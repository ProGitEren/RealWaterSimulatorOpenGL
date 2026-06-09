#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "graphics/Model.h"
#include "graphics/RockGenerator.h"
#include "ocean/GPUFFTOcean.h"
#include "ocean/GPUDisturbance.h"
#include "ocean/OceanMesh.h"
#include "water/GPURain.h"
#include "water/BoatPhysics.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <iomanip>
#include <deque>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>     // rand(), RAND_MAX, srand — not transitively guaranteed on MSVC
#include <filesystem>  // create_directories for the recording output dir

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"


unsigned int loadCubemap(const std::vector<std::string>& faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    stbi_set_flip_vertically_on_load(false);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);

        if (data) {
            GLenum format = GL_RGB;
            if (nrChannels == 1) format = GL_RED;
            else if (nrChannels == 3) format = GL_RGB;
            else if (nrChannels == 4) format = GL_RGBA;

            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, format, width, height, 0,
                format, GL_UNSIGNED_BYTE, data
            );
            stbi_image_free(data);
        } else {
            std::cout << "Cubemap face failed to load: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

namespace {
    constexpr float         kFixedDt          = 1.0f / 60.0f;
    constexpr float         kMaxAccumulatedTime = 0.25f;
    constexpr unsigned int  kOceanResolution  = 512;  // Stockham FFT — true 512, matches reference
    // Match achalpandeyy/OceanFFT exactly: 1024x1024 grid, 1m vertex spacing,
    // 1024m wide ocean patch. 1m spacing (vs the old 2m) is what lets the fine
    // FFT waves actually show up in the geometry instead of being smeared.
    constexpr int           kOceanMeshRes     = 1024;  // vertex grid — 1024x1024
    constexpr float         kOceanMeshTile    = 1.0f;  // meters per tile -> 1024m
    constexpr int           kPhysicsGridSize  = 150;   // CPU physics grid (rain ripples)
    constexpr float         kPhysicsTileSize  = 1.0f;
    // 256 entries = 256 loop iterations per fragment — safe budget.
    // SSBO avoids the constant-register limit that blocked uniform arrays.
    constexpr int           kMaxRipples       = 512;
    // Window framebuffer size (also used by the recording / readback path).
    constexpr int           kWindowWidth      = 1024;
    constexpr int           kWindowHeight     = 768;
    // Disturbance (wake/ripple) map resolution — must match the GPUDisturbance
    // ctor below and the 256.0 divisor in standard.frag.
    constexpr unsigned int  kDisturbResolution = 256;

    GLuint rippleSSBO = 0;
    glm::vec4 rippleStagingBuf[kMaxRipples]; // pre-allocated, no heap alloc per frame

    void initRippleSSBO() {
        glGenBuffers(1, &rippleSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, rippleSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, kMaxRipples * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rippleSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void uploadRipples(const Shader& shader, const std::deque<glm::vec3>& ripples) {
        const int total = static_cast<int>(ripples.size());
        const int count = std::min(total, kMaxRipples);
        shader.setInt("numRipples", count);
        if (count == 0) return;

        // Pack newest `count` entries into pre-allocated staging buffer
        const int offset = total - count;
        for (int i = 0; i < count; ++i)
            rippleStagingBuf[i] = glm::vec4(ripples[offset + i], 0.0f);

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, rippleSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, count * sizeof(glm::vec4), rippleStagingBuf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rippleSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
}


int main() {
    Window window(kWindowWidth, kWindowHeight, "Real-Time Water Simulator");
    if (!window.getGLFWWindow()) return -1;

    GLFWwindow* win = window.getGLFWWindow();

    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // --- Dear ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);   // installs GLFW callbacks (app polls, so safe)
    ImGui_ImplOpenGL3_Init("#version 460 core");

    Camera camera(glm::vec3(0.0f, 40.0f, 120.0f));

    Shader shader("../assets/shaders/standard.vert", "../assets/shaders/standard.frag");
    Shader debugWireframeShader("../assets/shaders/standard.vert", "../assets/shaders/debug_wireframe.frag");
    Shader rainShader("../assets/shaders/rain_gpu.vert", "../assets/shaders/rain_gpu.frag");
    Shader rainSplashShader("../assets/shaders/rain_splash.vert", "../assets/shaders/rain_splash.frag");
    Shader skyboxShader("../assets/shaders/skybox.vert", "../assets/shaders/skybox.frag");
    Shader objectShader("../assets/shaders/object.vert", "../assets/shaders/object.frag");
    objectShader.use();
    objectShader.setInt("skybox", 0);

    shader.use();
    shader.setInt("skybox", 0);
    shader.setInt("displacementMap", 1);
    shader.setInt("normalMap", 2);
    shader.setInt("disturbanceMap", 3);

    debugWireframeShader.use();
    debugWireframeShader.setInt("displacementMap", 1);
    debugWireframeShader.setInt("disturbanceMap", 3);

    skyboxShader.use();
    skyboxShader.setInt("skybox", 0);

    // --- SKYBOX ---
    float skyboxVertices[] = {
        -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
    };

    unsigned int skyboxVAO, skyboxVBO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Sky options (selectable in the UI). Each folder holds the 6 cube faces.
    const char* skyNames[] = { "sky 1", "sky 2", "sky 3", "environment" };
    const char* skyDirs[]  = { "sky_1", "sky_2", "sky_3", "environment_1" };
    const int   kNumSkies  = 4;
    unsigned int skyTextures[kNumSkies];
    for (int si = 0; si < kNumSkies; ++si) {
        const std::string base = std::string("../assets/textures/skybox/") + skyDirs[si] + "/";
        std::vector<std::string> faces = {
            base + "right.png", base + "left.png", base + "top.png",
            base + "bottom.png", base + "front.png", base + "back.png"
        };
        skyTextures[si] = loadCubemap(faces);
    }
    int currentSky = 0;                             // sky 1 == the previous default
    unsigned int cubemapTexture = skyTextures[currentSky];

    // --- OCEAN ---
    GPUFFTOcean    ocean(kOceanResolution, kOceanMeshRes * kOceanMeshTile, 15.0f, 35.0f, 2.0f);
    OceanMesh      oceanMesh(kOceanMeshRes, kOceanMeshTile);
    GPUDisturbance disturbance(kDisturbResolution, kOceanMeshRes * kOceanMeshTile);

    // --- OBJECTS ---
    // Poly Haven marble cliff (glTF + PBR textures). The procedural rock
    // generator is still available (generateRock) as a fallback.
    Model rock("../assets/models/rock_marble_cliff_05/marble_cliff_05_4k.gltf");
    if (!rock.loaded()) {
        std::cerr << "Rock model failed to load — using procedural fallback" << std::endl;
        std::vector<Vertex> rv; std::vector<unsigned int> ri;
        generateRock(3u, 1u, rv, ri);
        rock = Model(rv, ri);
    }
    rock.setScale(4.0f);
    rock.setPosition(glm::vec3(60.0f, -3.0f, 30.0f)); // partly out of the water

    // --- VEHICLES: jet-ski, yacht, big-ship ---
    // Loaded once each; given a random start position inside the bay plus a
    // heading and speed so they cruise across the water. Press P to pause/resume.
    Model jetski("../assets/models/jet-ski/scene.gltf");
    Model yacht ("../assets/models/yacht/scene.gltf");
    Model bigShip("../assets/models/big-ship/scene.gltf");

    struct Vehicle {
        Model* model;
        glm::vec3 pos;
        float heading;    // radians, yaw
        float speed;      // m/s
        float scale;
        float yOffset;    // sit at/just above the waterline
        float modelYaw;   // extra yaw so the model's forward axis aligns to heading
        float wakeWidth;  // hull half-width (m) -> physics beam (= 2*wakeWidth)
        float hullLength; // bow-to-stern length (m)
        float wakeScale;  // per-boat wake amplitude multiplier (ship=1.0 reference)
                          // -> one UI slider, but a jet-ski drops a small ripple
                          //    while the big ship throws a broad swell
        float turnVel   = 0.0f; // current yaw rate (rad/s) — drives banking
        float bankAngle = 0.0f; // smoothed lean into turns (rad)
        BoatPhysics phys; // force-based 6-DOF heave/pitch/roll state
    };

    // Sizes: jet-ski raw ~15u, yacht ~3068u, ship ~5159u -> scale to sane metres.
    auto frand = [](float a, float b) { return a + (b - a) * (float(rand()) / float(RAND_MAX)); };
    // Scales account for each model's baked node transform. The jet-ski's glTF
    // node matrix bakes in a 0.01 FBX scale, so its real size is only ~0.15u —
    // it needs a MUCH larger multiplier than the yacht/ship to be visible.
    // Last field (modelYaw) aligns each model's authored-forward axis with the
    // travel heading: jet-ski faces +90° off (-PI/2 correction), yacht is
    // reversed (+PI), big-ship is already correct (0).
    const float kHalfPi = 1.5707963f;
    const float kPiF    = 3.1415927f;
    std::vector<Vehicle> vehicles = {
        //         model     position                                              heading        spd  scale   yOff  modelYaw  wakeW  hullLen  wakeScale
        { &jetski,  glm::vec3(frand(-250,250), 0.0f, frand(-250,250)), frand(0, 6.28f), 16.0f, 120.0f, 1.5f, -kHalfPi,  4.0f,   5.0f,  0.22f },
        { &yacht,   glm::vec3(frand(-250,250), 0.0f, frand(-250,250)), frand(0, 6.28f),  8.0f, 0.03f,  0.0f,  kPiF,    14.0f,  90.0f,  0.70f },
        { &bigShip, glm::vec3(frand(-250,250), 0.0f, frand(-250,250)), frand(0, 6.28f),  5.0f, 0.02f,  0.0f,  0.0f,    20.0f, 110.0f,  1.00f },
    };
    // Configure each vehicle's buoyancy hull dimensions from its length/beam.
    for (Vehicle& v : vehicles) {
        v.phys.length = v.hullLength;
        v.phys.beam   = v.wakeWidth * 2.0f;
        v.phys.floatHeight = v.yOffset;
    }

    bool vehiclesMoving = true;   // toggled by P
    bool pKeyWasDown    = false;
    // --- Vehicle containment & collision avoidance (keeps boats off the cliffs
    // and apart from each other; water physics stays in BoatPhysics) ---
    const float kSoftRadius = 300.0f;  // centre-dist where steer-back reaches full strength
    const float kHullLimit  = 400.0f;  // hard cap: no hull tip past this (cliff faces ~440)
    const glm::vec2 kRockXZ = glm::vec2(60.0f, 30.0f); // central rock obstacle
    const float kRockAvoid  = 70.0f;   // steer away from the rock within this
    const float kRockHard   = 45.0f;   // hard cap: never enter this radius around the rock

    // Coastal cliffs: real Poly Haven scan (coastal_cliff_04 — a ~87m-wide low
    // shoreline cliff strip) arranged as a ring of coastline around the scene,
    // each piece rotated to face inward toward the water. Scaled up for a
    // mid-range coastal mountain look (taller than the raw 11m scan).
    Model coastalCliff("../assets/models/mountain_terrain/coastal_cliff_04_4k.gltf");

    // Each piece: position on the ring + scale (taller y for cliff height) +
    // yaw so the cliff FACE looks inward toward the water (the origin).
    struct MountainInstance { glm::vec3 pos; glm::vec3 scale; float rotY; };
    // Water is a SQUARE patch spanning ±512m. Place edge cliffs just inside the
    // edge and corner cliffs out at the square's corners so the whole boundary
    // (edges AND corners) is covered by overlapping land — the hard tile edge is
    // never visible.
    const float kEdge   = 470.0f;     // edge-piece distance (just inside ±512)
    const float kCorner = kEdge * 0.7071f; // corner pieces at the SAME radius as the edges
                                           // (axial ~332) so the ring is uniform — no
                                           // diagonal gaps/void between water and cliffs
    // The model's cliff face points along its local +Z. To aim that face from a
    // ring position (px,pz) back at the origin, rotate by atan2(-px,-pz).
    // kFaceOffset flips it if this scan happens to face outward.
    const float kFaceOffset = 0.0f;   // set to 3.14159f if faces point outward
    auto faceIn = [&](float px, float pz) { return std::atan2(-px, -pz) + kFaceOffset; };
    const float kCliffScale = 9.0f;   // base scale (~87m -> ~780m wide cliffs)
    const std::vector<MountainInstance> mountains = {
        { glm::vec3(    0.0f, -6.0f, -kEdge),  glm::vec3(kCliffScale, kCliffScale*1.6f, kCliffScale), faceIn(0.0f, -kEdge) },
        { glm::vec3(    0.0f, -6.0f,  kEdge),  glm::vec3(kCliffScale, kCliffScale*1.5f, kCliffScale), faceIn(0.0f,  kEdge) },
        { glm::vec3(-kEdge,   -6.0f,    0.0f), glm::vec3(kCliffScale, kCliffScale*1.7f, kCliffScale), faceIn(-kEdge, 0.0f) },
        { glm::vec3( kEdge,   -6.0f,    0.0f), glm::vec3(kCliffScale, kCliffScale*1.5f, kCliffScale), faceIn( kEdge, 0.0f) },
        { glm::vec3(-kCorner, -6.0f, -kCorner), glm::vec3(kCliffScale, kCliffScale*1.6f, kCliffScale), faceIn(-kCorner, -kCorner) },
        { glm::vec3( kCorner, -6.0f, -kCorner), glm::vec3(kCliffScale, kCliffScale*1.7f, kCliffScale), faceIn( kCorner, -kCorner) },
        { glm::vec3(-kCorner, -6.0f,  kCorner), glm::vec3(kCliffScale, kCliffScale*1.5f, kCliffScale), faceIn(-kCorner,  kCorner) },
        { glm::vec3( kCorner, -6.0f,  kCorner), glm::vec3(kCliffScale, kCliffScale*1.6f, kCliffScale), faceIn( kCorner,  kCorner) },
    };

    // Place boats at safe, non-overlapping spawn points inside the play area and
    // clear of the central rock (the random init positions could land on a rock,
    // outside the play zone, or on top of each other).
    for (size_t i = 0; i < vehicles.size(); ++i) {
        const float ri = vehicles[i].hullLength * 0.5f;
        for (int tries = 0; tries < 200; ++tries) {
            glm::vec2 p(frand(-200.0f, 200.0f), frand(-200.0f, 200.0f));
            if (glm::length(p) > 200.0f) continue;
            if (glm::length(p - kRockXZ) < kRockHard + ri + 20.0f) continue;
            bool ok = true;
            for (size_t j = 0; j < i; ++j) {
                const float rj = vehicles[j].hullLength * 0.5f;
                glm::vec2 q(vehicles[j].pos.x, vehicles[j].pos.z);
                if (glm::length(p - q) < ri + rj + 30.0f) { ok = false; break; }
            }
            if (ok) { vehicles[i].pos.x = p.x; vehicles[i].pos.z = p.y; break; }
        }
    }

    // --- RAIN (GPU-driven: drops live + update in an SSBO; instanced draw) ---
    GPURain         rainSystem(120000u); // max drops the GPU buffer holds
    initRippleSSBO();

    // --- LOOP STATE ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double lastX = kWindowWidth / 2.0;
    double lastY = kWindowHeight / 2.0;
    bool firstMouse = true;
    bool mouseCaptured = false; // first-person capture: hidden + locked cursor, free-look
    bool escWasPressed = false;

    bool wireframeMode   = false;
    bool tKeyWasPressed  = false;
    bool vKeyWasPressed  = false;
    bool kKeyWasPressed  = false;
    bool lKeyWasPressed  = false;
    bool cKeyWasPressed  = false;
    float tunePrintTimer = 0.0f;

    bool isRecording = false;
    int  frameCount  = 0;
    const int kMaxFrames = 600;

    // --- Force-based buoyancy tuning (applied to every vehicle each frame) ---
    float buoyancyStrength = 3.0f; // up-force per metre submerged (higher = floats higher/firmer)
    float buoyancyResponse = 2.0f; // heave damping (higher = settles faster, less bobbing)
    float pitchRollDamp    = 2.8f; // pitch/roll damping (higher = steadier, less rocking)
    float wakeStrength = 0.01f; // per-step ripple amplitude a moving vehicle injects (accumulates ~60x/s)
    bool  showCenterRock = true; // draw + collide the lone central rock (UI toggle)
    // --- Navigation realism (gradual turns, gentle wander, banking) ---
    float boatTurnRate = 0.45f; // max steer-back turn rate (rad/s) — gradual, not a snap
    float boatWander   = 0.12f; // gentle heading-weave amplitude so paths curve naturally
    float boatBank     = 0.1f;  // how hard boats lean into turns (visual roll); ~v*omega coordinated-turn

    // --- Rain (the only live UI section; everything above is baked to defaults) ---
    int   rainSpawnRate    = 50;     // drops/frame (intensity)
    float rainFallSpeed    = 77.0f;  // m/s
    float windDir          = 35.0f;  // degrees -> rain drift + streaks + OCEAN wave direction
    float windStrength     = 0.5f;   // 0..1 -> rain slant (drift velocity)
    float rainDropSize     = 1.0f;   // streak length + thickness
    float rainOpacity      = 0.22f;  // streak opacity
    float rainSplashHeight = 1.0f;   // splash jet height
    float rippleLifetime   = 3.0f;   // ring lifetime (s)
    float ringStrength     = 1.0f;   // ring distortion multiplier
    float ringSpeed        = 5.0f;   // ring expansion speed (m/s)

    // --- Water surface look (standard.frag) ---
    glm::vec3 deepColor      = glm::vec3(1.0f/255.0f, 9.0f/255.0f, 22.0f/255.0f);
    glm::vec3 shallowColor   = glm::vec3(15.0f/255.0f, 77.0f/255.0f, 102.0f/255.0f);
    float     depthFalloff   = 0.020f; // how fast troughs darken into deep colour
    float     midWaveDetail  = 0.4f;   // procedural surface-ripple amount
    float     cKeySplash     = 3.0f;   // C-key disturbance amplitude
    // --- Lighting (standard.frag) ---
    float     reflectStrength   = 1.8f;   // sky-reflection brightness (HDR boost)
    glm::vec3 horizonColor      = glm::vec3(0.55f, 0.68f, 0.82f);
    glm::vec3 scatterColor      = glm::vec3(0.08f, 0.45f, 0.35f);
    glm::vec3 sunColor          = glm::vec3(1.0f, 0.96f, 0.88f);
    float     sunAzimuth        = 31.0f;  // degrees
    float     sunElevation      = 60.0f;  // degrees
    float     sunGlint          = 4.0f;   // tight specular highlight intensity
    float     sunGlitter        = 0.4f;   // broad sparkle intensity
    float     hdrExposure       = 1.4f;

    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!window.shouldClose()) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Start the ImGui frame; io.WantCapture* below gates app input so the
        // panel doesn't drive the camera while you interact with it.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        // While captured, ImGui ignores the (locked, centred) cursor so the panel
        // doesn't react; it's interactive again the moment you release with Esc.
        if (mouseCaptured) io.ConfigFlags |=  ImGuiConfigFlags_NoMouse;
        else               io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

        // Left-click on the 3D view (not the panel) captures the mouse for
        // first-person look: pointer hidden, locked to the window, raw deltas so
        // you can keep turning past the edge of the screen.
        if (!mouseCaptured && !io.WantCaptureMouse &&
            glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            mouseCaptured = true;
            firstMouse    = true; // avoid a look jump on capture
        }

        // Esc: release the cursor if captured, otherwise quit (edge-triggered).
        const bool escDown = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !escWasPressed) {
            if (mouseCaptured) {
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                mouseCaptured = false;
            } else {
                glfwSetWindowShouldClose(win, true);
            }
        }
        escWasPressed = escDown;

        // Wireframe toggle — T or V
        const bool tKeyPressed = glfwGetKey(win, GLFW_KEY_T) == GLFW_PRESS;
        const bool vKeyPressed = glfwGetKey(win, GLFW_KEY_V) == GLFW_PRESS;
        if ((tKeyPressed && !tKeyWasPressed) || (vKeyPressed && !vKeyWasPressed)) {
            wireframeMode = !wireframeMode;
            std::cout << "Render mode: " << (wireframeMode ? "wireframe (no shading)" : "normal shaded") << std::endl;
        }
        tKeyWasPressed = tKeyPressed;
        vKeyWasPressed = vKeyPressed;

        // Wind speed — K(+0.5) / L(-0.5), range [5, 10]
        const bool kKeyPressed = glfwGetKey(win, GLFW_KEY_K) == GLFW_PRESS;
        const bool lKeyPressed = glfwGetKey(win, GLFW_KEY_L) == GLFW_PRESS;
        if (kKeyPressed && !kKeyWasPressed) {
            ocean.setWindSpeed(std::min(10.0f, ocean.getWindSpeed() + 0.5f));
            std::cout << std::fixed << std::setprecision(1) << "windSpeed=" << ocean.getWindSpeed() << std::endl;
        }
        if (lKeyPressed && !lKeyWasPressed) {
            ocean.setWindSpeed(std::max(5.0f, ocean.getWindSpeed() - 0.5f));
            std::cout << std::fixed << std::setprecision(1) << "windSpeed=" << ocean.getWindSpeed() << std::endl;
        }
        kKeyWasPressed = kKeyPressed;
        lKeyWasPressed = lKeyPressed;

        // Camera movement — only while in first-person capture (click to enter)
        if (mouseCaptured) {
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(0, deltaTime);
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(1, deltaTime);
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(2, deltaTime);
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(3, deltaTime);
            if (glfwGetKey(win, GLFW_KEY_SPACE)      == GLFW_PRESS) camera.ProcessKeyboard(4, deltaTime);
            if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.ProcessKeyboard(5, deltaTime);
        }

        // C key — fire a GPU wave disturbance 40m in front of camera (debounced)
        const bool cKeyPressed = glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS;
        if (cKeyPressed && !cKeyWasPressed) {
            glm::vec3 flatFront = glm::normalize(glm::vec3(camera.Front.x, 0.0f, camera.Front.z));
            glm::vec2 impactXZ  = glm::vec2(camera.Position.x, camera.Position.z)
                                + glm::vec2(flatFront.x, flatFront.z) * 40.0f;
            disturbance.disturb(impactXZ, cKeySplash);
        }
        cKeyWasPressed = cKeyPressed;

        // P — toggle vehicle movement on/off (debounced)
        const bool pKeyDown = glfwGetKey(win, GLFW_KEY_P) == GLFW_PRESS;
        if (pKeyDown && !pKeyWasDown) {
            vehiclesMoving = !vehiclesMoving;
            std::cout << "Vehicles: " << (vehiclesMoving ? "moving" : "stopped") << std::endl;
        }
        pKeyWasDown = pKeyDown;

        // Live ocean tuning (hold to ramp)
        const float kTuneRate = deltaTime * 3.0f;
        bool tuning = false;
        if (glfwGetKey(win, GLFW_KEY_UP)    == GLFW_PRESS) { ocean.setHeightScale (ocean.getHeightScale()  + kTuneRate * 5.0f);                  tuning = true; }
        if (glfwGetKey(win, GLFW_KEY_DOWN)  == GLFW_PRESS) { ocean.setHeightScale (std::max(0.1f, ocean.getHeightScale()  - kTuneRate * 5.0f));  tuning = true; }
        if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) { ocean.setTimeScale   (ocean.getTimeScale()    + kTuneRate);                         tuning = true; }
        if (glfwGetKey(win, GLFW_KEY_LEFT)  == GLFW_PRESS) { ocean.setTimeScale   (std::max(0.1f, ocean.getTimeScale()   - kTuneRate));          tuning = true; }
        if (glfwGetKey(win, GLFW_KEY_N)     == GLFW_PRESS) { ocean.setChoppiness  (std::min(2.5f, ocean.getChoppiness()  + kTuneRate * 0.5f));   tuning = true; }
        if (glfwGetKey(win, GLFW_KEY_M)     == GLFW_PRESS) { ocean.setChoppiness  (std::max(0.0f, ocean.getChoppiness()  - kTuneRate * 0.5f));   tuning = true; }

        if (tuning) {
            tunePrintTimer -= deltaTime;
            if (tunePrintTimer <= 0.0f) {
                tunePrintTimer = 0.25f;
                std::cout << std::fixed << std::setprecision(2)
                          << "heightScale=" << ocean.getHeightScale()
                          << "  timeScale="  << ocean.getTimeScale()
                          << "  choppiness=" << ocean.getChoppiness()
                          << std::endl;
            }
        }

        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS && !isRecording) {
            std::cout << "RECORDING STARTED!" << std::endl;
            // Ensure the output dir exists (cross-platform). stbi_write_png
            // silently fails if "frames/" is missing.
            std::error_code ec;
            std::filesystem::create_directories("frames", ec);
            isRecording = true;
            frameCount = 0;
        }

        // First-person mouse look — only while captured. In GLFW_CURSOR_DISABLED
        // the cursor is hidden + locked and reports unbounded virtual deltas, so
        // you can keep turning past the screen edge.
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
        if (mouseCaptured)
            camera.ProcessMouseMovement(static_cast<float>(xpos - lastX), static_cast<float>(lastY - ypos));
        lastX = xpos;
        lastY = ypos;

        // Rain wind drift (from the wind UI), used by both update and render.
        const float wr = glm::radians(windDir);
        const glm::vec2 rainWindDrift = glm::vec2(std::cos(wr), std::sin(wr)) * (windStrength * 25.0f);

        // Physics update
        static float accumulator = 0.0f;
        accumulator = std::min(accumulator + deltaTime, kMaxAccumulatedTime);
        while (accumulator >= kFixedDt) {
            ocean.update(kFixedDt);
            disturbance.update(kFixedDt);

            // Vehicles: advance NAVIGATION (XZ + heading). Each boat blends three
            // urges into a desired heading — return toward centre, avoid the other
            // boats, avoid the central rock — then turns toward it (rate-limited,
            // sharper near the edge). Hard clamps below guarantee it never phases
            // through the cliffs/rock or overlaps another boat. (Heave/pitch/roll
            // come from BoatPhysics, stepped once per frame after the readback.)
            if (vehiclesMoving) {
                for (size_t vi = 0; vi < vehicles.size(); ++vi) {
                    Vehicle& v = vehicles[vi];
                    glm::vec3 dir(std::sin(v.heading), 0.0f, std::cos(v.heading));
                    v.pos += dir * v.speed * kFixedDt;

                    const glm::vec2 p(v.pos.x, v.pos.z);
                    const float distXZ = glm::length(p);
                    const float ri = v.hullLength * 0.5f;
                    const float prevHeading = v.heading;

                    glm::vec2 desired(dir.x, dir.z); // keep going, then bias

                    // (a) Boundary: pull toward centre, ramping in past 0.7*kSoftRadius.
                    if (distXZ > kSoftRadius * 0.7f && distXZ > 1e-3f) {
                        float w = glm::clamp((distXZ - kSoftRadius * 0.7f) / (kSoftRadius * 0.3f), 0.0f, 1.0f);
                        desired += (-p / distXZ) * (w * 2.5f);
                    }
                    // (b) Separation from the other boats (size-aware).
                    for (size_t vj = 0; vj < vehicles.size(); ++vj) {
                        if (vj == vi) continue;
                        glm::vec2 d = p - glm::vec2(vehicles[vj].pos.x, vehicles[vj].pos.z);
                        float dd = glm::length(d);
                        float keep = ri + vehicles[vj].hullLength * 0.5f + 30.0f;
                        if (dd > 1e-3f && dd < keep)
                            desired += (d / dd) * ((1.0f - dd / keep) * 2.2f);
                    }
                    // (c) Central rock (skip when the rock is toggled off).
                    if (showCenterRock) {
                        glm::vec2 d = p - kRockXZ;
                        float dd = glm::length(d);
                        float keep = kRockAvoid + ri;
                        if (dd > 1e-3f && dd < keep)
                            desired += (d / dd) * ((1.0f - dd / keep) * 3.0f);
                    }

                    // Turn toward the desired heading, rate-limited; sharper when
                    // urgently near the hull limit so we never run out of room.
                    if (glm::length(desired) > 1e-3f) {
                        glm::vec2 nd = glm::normalize(desired);
                        float target = std::atan2(nd.x, nd.y);
                        float dh = std::atan2(std::sin(target - v.heading), std::cos(target - v.heading));
                        float urgency = glm::clamp((distXZ + ri - (kHullLimit - 90.0f)) / 90.0f, 0.0f, 1.0f);
                        float maxTurn = boatTurnRate * (1.0f + 3.0f * urgency) * kFixedDt;
                        v.heading += glm::clamp(dh, -maxTurn, maxTurn);
                    }
                    // Gentle weave only when comfortably clear of everything.
                    if (distXZ < kSoftRadius * 0.7f)
                        v.heading += boatWander * std::sin(currentFrame * 0.25f + float(vi) * 2.3f) * kFixedDt;

                    v.turnVel = (v.heading - prevHeading) / kFixedDt; // yaw rate -> banking

                    // Wake churns off the STERN (trailing waterline), not the hull
                    // centre, so it streams from where the boat meets the water.
                    // Shift to the VISIBLE hull centre first (boat meshes can sit
                    // off their glTF origin, which made the wake appear off to one
                    // side — worst on the big boats), then back to the stern.
                    glm::vec3 lc = v.scale * v.model->localCenter();
                    float vyaw = v.heading + v.modelYaw;
                    glm::vec2 hullCtr = p + glm::vec2(lc.x * std::cos(vyaw) + lc.z * std::sin(vyaw),
                                                     -lc.x * std::sin(vyaw) + lc.z * std::cos(vyaw));

                    // Size the wake to the boat. The disturbance map is 256 texels
                    // over the ocean, so one texel ~= oceanSize/256 metres. Footprint
                    // tracks the hull's beam: a tight ripple for the jet-ski, a broad
                    // swell for the ship — instead of one 20 m bump that engulfs the
                    // little boats.
                    const float kTexelM   = (kOceanMeshRes * kOceanMeshTile) / static_cast<float>(kDisturbResolution); // ~4 m
                    float sigmaTexels = glm::clamp(v.wakeWidth / kTexelM, 1.5f, 5.0f);
                    float sigmaM      = sigmaTexels * kTexelM;

                    // Inject just BEHIND the transom (by most of the footprint radius)
                    // so the swell trails the boat as a separation wake rather than
                    // rising up underneath and "submerging" it.
                    glm::vec2 sternXZ = hullCtr - glm::vec2(dir.x, dir.z)
                                        * (v.hullLength * 0.5f + sigmaM * 0.6f);
                    disturbance.disturb(sternXZ,
                                        wakeStrength * v.wakeScale * glm::min(1.0f, v.speed / 12.0f),
                                        sigmaTexels);
                }

                // HARD safety net — guarantees no boat-boat overlap and no phasing
                // through the cliffs or the rock, whatever the steering above did.
                for (size_t i = 0; i < vehicles.size(); ++i)       // push overlapping boats apart
                    for (size_t j = i + 1; j < vehicles.size(); ++j) {
                        glm::vec2 d = glm::vec2(vehicles[i].pos.x, vehicles[i].pos.z)
                                    - glm::vec2(vehicles[j].pos.x, vehicles[j].pos.z);
                        float dl  = glm::length(d);
                        float gap = vehicles[i].hullLength * 0.5f + vehicles[j].hullLength * 0.5f + 10.0f;
                        if (dl < gap && dl > 1e-3f) {
                            glm::vec2 push = (d / dl) * ((gap - dl) * 0.5f);
                            vehicles[i].pos.x += push.x; vehicles[i].pos.z += push.y;
                            vehicles[j].pos.x -= push.x; vehicles[j].pos.z -= push.y;
                        }
                    }
                for (Vehicle& v : vehicles) {                       // then containment has final say
                    glm::vec2 p(v.pos.x, v.pos.z);
                    const float ri = v.hullLength * 0.5f;
                    if (showCenterRock) {                          // never enter the rock
                        glm::vec2 rd = p - kRockXZ;
                        float rl = glm::length(rd), rcap = kRockHard + ri;
                        if (rl < rcap && rl > 1e-3f) { p = kRockXZ + (rd / rl) * rcap; }
                    }
                    float d = glm::length(p), cap = kHullLimit - ri; // never past the hull limit
                    if (d > cap && d > 1e-3f) { p *= cap / d; }
                    v.pos.x = p.x; v.pos.z = p.y;
                }
            }
            accumulator -= kFixedDt;
        }

        // GPU rain: advance ONCE per frame (not per substep) — drops are visual,
        // so one compute dispatch over the whole frame's dt is identical-looking
        // but avoids redundant dispatches on slow frames.
        rainSystem.update(deltaTime, camera.Position, rainWindDrift,
                          rainFallSpeed, rainSpawnRate, rippleLifetime);

        // Read the ocean displacement back to the CPU ONCE per frame (after all
        // fixed substeps) for object height sampling — see readbackDisplacement().
        ocean.readbackDisplacement();

        // --- Vehicle buoyancy (force-based 6-DOF), once per frame ---
        // Step the rigid-body buoyancy using the (now-current) ocean heights.
        // Navigation set v.pos/heading above; physics solves heave/pitch/roll.
        {
            auto surfFn = [&](float x, float z) { return ocean.sampleSurfaceHeight(x, z); };
            for (Vehicle& v : vehicles) {
                v.phys.buoyancy    = buoyancyStrength;
                v.phys.linearDamp  = buoyancyResponse;
                v.phys.angularDamp = pitchRollDamp;
                v.phys.step(deltaTime, v.pos, v.heading + v.modelYaw, surfFn);
            }
        }

        // --- ImGui control panel (Rain only; everything else baked to defaults) ---
        {
            ImGui::Begin("Controls");
            ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Rain", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SeparatorText("Intensity");
                ImGui::SliderInt  ("spawn rate", &rainSpawnRate, 0, 500);
                ImGui::SliderFloat("fall speed", &rainFallSpeed, 20.0f, 140.0f, "%.0f m/s");
                ImGui::SliderFloat("drop size",  &rainDropSize, 0.3f, 3.0f);
                ImGui::SliderFloat("opacity",    &rainOpacity, 0.0f, 1.0f);

                ImGui::SeparatorText("Wind (rain + waves)");
                if (ImGui::SliderFloat("direction", &windDir, 0.0f, 360.0f, "%.0f deg"))
                    ocean.setWindAngle(windDir);
                ImGui::SliderFloat("strength", &windStrength, 0.0f, 1.0f);

                ImGui::SeparatorText("Splash & ripples");
                ImGui::SliderFloat("splash height",   &rainSplashHeight, 0.0f, 3.0f);
                ImGui::SliderFloat("ripple lifetime", &rippleLifetime, 0.5f, 5.0f, "%.1f s");
                ImGui::SliderFloat("ring speed",      &ringSpeed, 0.5f, 15.0f, "%.1f m/s");
                ImGui::SliderFloat("ring strength",   &ringStrength, 0.0f, 3.0f);
            }

            if (ImGui::CollapsingHeader("Water")) {
                ImGui::SeparatorText("Waves (FFT)");
                float wind = ocean.getWindSpeed();
                if (ImGui::SliderFloat("wind speed", &wind, 2.0f, 30.0f, "%.1f m/s")) ocean.setWindSpeed(wind);
                float wh = ocean.getHeightScale();
                if (ImGui::SliderFloat("wave height", &wh, 0.0f, 6.0f)) ocean.setHeightScale(wh);
                float chop = ocean.getChoppiness();
                if (ImGui::SliderFloat("choppiness", &chop, 0.0f, 3.0f)) ocean.setChoppiness(chop);
                float hs = ocean.getHorizontalScale();
                if (ImGui::SliderFloat("horizontal displace", &hs, 0.0f, 1.2f)) ocean.setHorizontalScale(hs);
                float ts = ocean.getTimeScale();
                if (ImGui::SliderFloat("time scale", &ts, 0.0f, 4.0f)) ocean.setTimeScale(ts);

                ImGui::SeparatorText("Spectrum (rebuilds on change)");
                float sm = ocean.getSeaMaturity();
                if (ImGui::SliderFloat("sea maturity", &sm, 0.84f, 4.0f)) ocean.setSeaMaturity(sm);
                float amp = ocean.getAmplitude();
                if (ImGui::SliderFloat("overall amplitude", &amp, 50.0f, 3000.0f, "%.0f")) ocean.setAmplitude(amp);

                ImGui::SeparatorText("Surface");
                ImGui::SliderFloat("depth tint falloff", &depthFalloff, 0.02f, 0.6f);
                ImGui::SliderFloat("mid-wave detail", &midWaveDetail, 0.0f, 3.0f);

                ImGui::SeparatorText("Colour");
                ImGui::ColorEdit3("deep water", &deepColor.x);
                ImGui::ColorEdit3("shallow / crest", &shallowColor.x);

                ImGui::SeparatorText("Interaction");
                ImGui::SliderFloat("boat wake strength", &wakeStrength, 0.0f, 0.05f, "%.3f");
                ImGui::SliderFloat("C-key splash", &cKeySplash, 0.0f, 10.0f);
            }

            if (ImGui::CollapsingHeader("Lighting")) {
                ImGui::SeparatorText("Sky / environment");
                if (ImGui::Combo("sky", &currentSky, skyNames, kNumSkies))
                    cubemapTexture = skyTextures[currentSky];

                ImGui::SeparatorText("Reflection & sky");
                ImGui::SliderFloat("reflection strength", &reflectStrength, 0.0f, 4.0f);
                ImGui::ColorEdit3("horizon sky tint", &horizonColor.x);

                ImGui::SeparatorText("Sun");
                ImGui::SliderFloat("sun azimuth", &sunAzimuth, 0.0f, 360.0f, "%.0f deg");
                ImGui::SliderFloat("sun elevation", &sunElevation, 0.0f, 90.0f, "%.0f deg");
                ImGui::ColorEdit3("sun colour", &sunColor.x);
                ImGui::SliderFloat("sun glint", &sunGlint, 0.0f, 12.0f);
                ImGui::SliderFloat("sun glitter", &sunGlitter, 0.0f, 2.0f);

                ImGui::SeparatorText("Scatter & tone");
                ImGui::ColorEdit3("scatter colour", &scatterColor.x);
                ImGui::SliderFloat("HDR exposure", &hdrExposure, 0.2f, 4.0f);
            }

            if (ImGui::CollapsingHeader("Vehicles")) {
                ImGui::SeparatorText("Speed (m/s)");
                for (size_t i = 0; i < vehicles.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    const char* name = (i == 0) ? "jet-ski" : (i == 1) ? "yacht" : "big-ship";
                    ImGui::SliderFloat(name, &vehicles[i].speed, 0.0f, 30.0f, "%.1f");
                    ImGui::PopID();
                }
            }
            if (ImGui::CollapsingHeader("Scene")) {
                ImGui::Checkbox("Central rock", &showCenterRock);
            }
            ImGui::End();
        }

        // --- RENDER ---
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight()),
            0.1f, 4000.0f
        );
        const glm::mat4 view = camera.GetViewMatrix();

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ocean.getDisplacementTexture());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ocean.getNormalTexture());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, disturbance.getHeightTexture());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);

        const std::deque<glm::vec3>& activeRipples = rainSystem.getActiveRipples();

        if (wireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            debugWireframeShader.use();
            debugWireframeShader.setMat4("projection", projection);
            debugWireframeShader.setMat4("view", view);
            debugWireframeShader.setFloat("oceanSize", ocean.getOceanSize());
            debugWireframeShader.setFloat("floorY", -10000.0f);
            debugWireframeShader.setInt("applyOceanDisplacement", 1);
            oceanMesh.draw(debugWireframeShader);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        } else {
            shader.use();
            shader.setMat4("projection", projection);
            shader.setMat4("view", view);
            shader.setVec3("viewPos", camera.Position);
            shader.setFloat("oceanSize", ocean.getOceanSize()); // used by both vert and frag
            shader.setFloat("floorY", -10000.0f);
            shader.setFloat("time", currentFrame);
            shader.setInt("applyOceanDisplacement", 1);
            shader.setFloat("uRippleLifetime", rippleLifetime);
            shader.setFloat("uRingSpeed",      ringSpeed);
            shader.setFloat("uRingStrength",   ringStrength);
            // water look + lighting uniforms
            {
                float saz = glm::radians(sunAzimuth), sel = glm::radians(sunElevation);
                glm::vec3 sunDir = glm::normalize(glm::vec3(std::cos(sel) * std::cos(saz),
                                                            std::sin(sel),
                                                            std::cos(sel) * std::sin(saz)));
                shader.setVec3 ("uDeepColor",         deepColor);
                shader.setVec3 ("uShallowColor",      shallowColor);
                shader.setFloat("uDepthFalloff",      depthFalloff);
                shader.setFloat("uMidWaveDetail",     midWaveDetail);
                shader.setFloat("uReflectStrength",   reflectStrength);
                shader.setVec3 ("uHorizonColor",      horizonColor);
                shader.setVec3 ("uScatterColor",      scatterColor);
                shader.setVec3 ("uSunColor",          sunColor);
                shader.setVec3 ("uSunDir",            sunDir);
                shader.setFloat("uSunGlint",          sunGlint);
                shader.setFloat("uSunGlitter",        sunGlitter);
                shader.setFloat("uExposure",          hdrExposure);
            }
            uploadRipples(shader, activeRipples);
            oceanMesh.draw(shader);

            // --- OBJECTS (solid, depth-tested, before transparent skybox/rain) ---
            objectShader.use();
            objectShader.setMat4("projection", projection);
            objectShader.setMat4("view", view);
            objectShader.setVec3("viewPos", camera.Position);
            objectShader.setVec3("baseColor", glm::vec3(0.42f, 0.40f, 0.38f)); // grey rock
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
            if (showCenterRock) rock.draw(objectShader);

            // --- COASTAL CLIFFS: real scanned cliff, ringed + facing inward ---
            for (const MountainInstance& m : mountains) {
                coastalCliff.setPosition(m.pos);
                coastalCliff.setScale(m.scale);
                coastalCliff.setRotationY(m.rotY);
                coastalCliff.draw(objectShader);
            }

            // --- VEHICLES: draw using the force-based 6-DOF physics result ---
            // BoatPhysics (stepped above, per frame) solved heave/pitch/roll from
            // per-facet buoyancy forces; we just read its position/orientation.
            for (Vehicle& v : vehicles) {
                v.model->setPosition(v.phys.position);
                v.model->setScale(v.scale);
                // Bank into turns: smoothly lean about the travel axis, scaled by
                // yaw rate * speed; eases back to level when straight or stopped.
                const float bankTarget = vehiclesMoving
                    ? glm::clamp(-v.turnVel * v.speed * boatBank, -0.35f, 0.35f) : 0.0f;
                v.bankAngle += (bankTarget - v.bankAngle) * glm::min(1.0f, deltaTime * 3.0f);
                const glm::vec3 fwdAxis(std::sin(v.heading), 0.0f, std::cos(v.heading));
                v.model->setOrientation(glm::angleAxis(v.bankAngle, fwdAxis) * v.phys.orientation);
                v.model->draw(objectShader);
            }
        }

        if (!wireframeMode) {
            // Skybox — renders at far plane, no depth write
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            skyboxShader.use();
            skyboxShader.setMat4("view", glm::mat4(glm::mat3(camera.GetViewMatrix())));
            skyboxShader.setMat4("projection", projection);
            glBindVertexArray(skyboxVAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);


            // Rain after skybox — GPU instanced draw (streaks + splashes, SSBO).
            rainSystem.render(rainShader, rainSplashShader, projection, view,
                              rainWindDrift, rainDropSize,
                              rainOpacity, rainSplashHeight);
        }

        // Recording
        if (isRecording) {
            unsigned char* pixels        = new unsigned char[kWindowWidth * kWindowHeight * 3];
            unsigned char* flippedPixels = new unsigned char[kWindowWidth * kWindowHeight * 3];
            glReadPixels(0, 0, kWindowWidth, kWindowHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels);
            for (int y = 0; y < kWindowHeight; ++y) {
                memcpy(flippedPixels + (kWindowHeight - 1 - y) * kWindowWidth * 3,
                       pixels        + y * kWindowWidth * 3,
                       kWindowWidth * 3);
            }
            std::string filename = "frames/frame_" + std::to_string(frameCount) + ".png";
            stbi_write_png(filename.c_str(), kWindowWidth, kWindowHeight, 3, flippedPixels, kWindowWidth * 3);
            delete[] pixels;
            delete[] flippedPixels;

            if (++frameCount >= kMaxFrames) {
                isRecording = false;
                std::cout << "RECORDING FINISHED!" << std::endl;
            }
        }

        // Draw the ImGui panel on top of the scene, then present.
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        window.swapBuffers();
        window.pollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glDeleteBuffers(1, &skyboxVBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteTextures(1, &cubemapTexture);

    return 0;
}
