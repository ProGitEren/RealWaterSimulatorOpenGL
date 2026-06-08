#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "graphics/Model.h"
#include "graphics/RockGenerator.h"
#include "graphics/Texture.h"
#include "ocean/GPUFFTOcean.h"
#include "ocean/GPUDisturbance.h"
#include "ocean/OceanMesh.h"
#include "water/WaterSimulation.h"
#include "water/RainSystem.h"

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

    GLuint rippleSSBO = 0;
    glm::vec4 rippleStagingBuf[512]; // pre-allocated, no heap alloc per frame

    void initRippleSSBO() {
        glGenBuffers(1, &rippleSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, rippleSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 512 * sizeof(glm::vec4), nullptr, GL_DYNAMIC_DRAW);
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
    Window window(1024, 768, "Real-Time Water Simulator");
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
    Shader rainShader("../assets/shaders/rain.vert", "../assets/shaders/rain.frag");
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

    std::vector<std::string> faces = {
        "../assets/textures/skybox/right.png",
        "../assets/textures/skybox/left.png",
        "../assets/textures/skybox/top.png",
        "../assets/textures/skybox/bottom.png",
        "../assets/textures/skybox/front.png",
        "../assets/textures/skybox/back.png"
    };
    unsigned int cubemapTexture = loadCubemap(faces);

    // --- OCEAN ---
    GPUFFTOcean    ocean(kOceanResolution, kOceanMeshRes * kOceanMeshTile, 8.0f, 35.0f, 1.6f);
    OceanMesh      oceanMesh(kOceanMeshRes, kOceanMeshTile);
    GPUDisturbance disturbance(256u, kOceanMeshRes * kOceanMeshTile);

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
        float heading;   // radians, yaw
        float speed;     // m/s
        float scale;
        float yOffset;   // sit at/just above the waterline
        float modelYaw;  // extra yaw so the model's forward axis aligns to heading
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
        { &jetski,  glm::vec3(frand(-250,250), 0.0f, frand(-250,250)), frand(0, 6.28f), 16.0f, 120.0f, 1.5f, -kHalfPi },
        { &yacht,   glm::vec3(frand(-250,250), 0.0f, frand(-250,250)), frand(0, 6.28f),  8.0f, 0.03f,  0.0f,  kPiF    },
        { &bigShip, glm::vec3(frand(-250,250), 0.0f, frand(-250,250)), frand(0, 6.28f),  5.0f, 0.02f,  0.0f,  0.0f    },
    };
    bool vehiclesMoving = true;   // toggled by P
    bool pKeyWasDown    = false;
    const float kBayBound = 430.0f; // turn the vehicles back inside this radius

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
    const float kCorner = 470.0f;     // corner-piece axial offset -> sits at ±470,±470
    // The model's cliff face points along its local +Z. To aim that face from a
    // ring position (px,pz) back at the origin, rotate by atan2(-px,-pz).
    // kFaceOffset flips it if this scan happens to face outward.
    const float kFaceOffset = 0.0f;   // set to 3.14159f if faces point outward
    auto faceIn = [&](float px, float pz) { return std::atan2(-px, -pz) + kFaceOffset; };
    const float s = 9.0f;             // base scale (~87m -> ~780m wide cliffs)
    const std::vector<MountainInstance> mountains = {
        { glm::vec3(    0.0f, -6.0f, -kEdge),  glm::vec3(s, s*1.6f, s), faceIn(0.0f, -kEdge) },
        { glm::vec3(    0.0f, -6.0f,  kEdge),  glm::vec3(s, s*1.5f, s), faceIn(0.0f,  kEdge) },
        { glm::vec3(-kEdge,   -6.0f,    0.0f), glm::vec3(s, s*1.7f, s), faceIn(-kEdge, 0.0f) },
        { glm::vec3( kEdge,   -6.0f,    0.0f), glm::vec3(s, s*1.5f, s), faceIn( kEdge, 0.0f) },
        { glm::vec3(-kCorner, -6.0f, -kCorner), glm::vec3(s, s*1.6f, s), faceIn(-kCorner, -kCorner) },
        { glm::vec3( kCorner, -6.0f, -kCorner), glm::vec3(s, s*1.7f, s), faceIn( kCorner, -kCorner) },
        { glm::vec3(-kCorner, -6.0f,  kCorner), glm::vec3(s, s*1.5f, s), faceIn(-kCorner,  kCorner) },
        { glm::vec3( kCorner, -6.0f,  kCorner), glm::vec3(s, s*1.6f, s), faceIn( kCorner,  kCorner) },
    };

    // --- PHYSICS (rain ripples) ---
    WaterSimulation water(kPhysicsGridSize, kPhysicsTileSize, kFixedDt, 8.0f, 0.998f);
    RainSystem      rainSystem(kPhysicsGridSize, kPhysicsTileSize);
    initRippleSSBO();

    // --- LOOP STATE ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double lastX = 1024.0 / 2.0;
    double lastY = 768.0 / 2.0;
    bool firstMouse = true;

    bool wireframeMode   = false;
    bool tKeyWasPressed  = false;
    bool vKeyWasPressed  = false;
    bool kKeyWasPressed  = false;
    bool lKeyWasPressed  = false;
    bool cKeyWasPressed  = false;
    float tunePrintTimer = 0.0f;

    bool isRecording = false;
    int  frameCount  = 0;
    const int MAX_FRAMES   = 600;
    const int screenWidth  = 1024;
    const int screenHeight = 768;

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

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, true);

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

        // Camera movement (suppressed while ImGui is capturing the keyboard)
        if (!io.WantCaptureKeyboard) {
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
            disturbance.disturb(impactXZ, 3.0f);
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

        // Mouse look (suppressed while ImGui wants the mouse, e.g. dragging a
        // slider; lastX/lastY still track so there's no jump when capture ends)
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
        if (!io.WantCaptureMouse)
            camera.ProcessMouseMovement(static_cast<float>(xpos - lastX), static_cast<float>(lastY - ypos));
        lastX = xpos;
        lastY = ypos;

        // Physics update
        static float accumulator = 0.0f;
        accumulator = std::min(accumulator + deltaTime, kMaxAccumulatedTime);
        while (accumulator >= kFixedDt) {
            water.update();
            rainSystem.update(kFixedDt, camera.Position, &water);
            ocean.update(kFixedDt);
            disturbance.update(kFixedDt);

            // Vehicles: advance along heading; if past the bay bound, turn back
            // toward the centre so they stay on the water.
            if (vehiclesMoving) {
                for (Vehicle& v : vehicles) {
                    glm::vec3 dir(std::sin(v.heading), 0.0f, std::cos(v.heading));
                    v.pos += dir * v.speed * kFixedDt;
                    float distXZ = std::sqrt(v.pos.x * v.pos.x + v.pos.z * v.pos.z);
                    if (distXZ > kBayBound) {
                        // steer heading toward the origin
                        v.heading = std::atan2(-v.pos.x, -v.pos.z);
                    }
                }
            }
            accumulator -= kFixedDt;
        }

        // Read the ocean displacement back to the CPU ONCE per frame (after all
        // fixed substeps) for object height sampling — see readbackDisplacement().
        ocean.readbackDisplacement();

        // --- ImGui control panel ---
        {
            ImGui::Begin("Controls");
            ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::Text("ocean readback: %.2f ms", ocean.getLastReadbackMs());
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Ocean", ImGuiTreeNodeFlags_DefaultOpen)) {
                float wind = ocean.getWindSpeed();
                if (ImGui::SliderFloat("wind speed",   &wind,       5.0f, 10.0f)) ocean.setWindSpeed(wind);
                float heightScale = ocean.getHeightScale();
                if (ImGui::SliderFloat("height scale", &heightScale, 0.1f, 4.0f)) ocean.setHeightScale(heightScale);
                float chop = ocean.getChoppiness();
                if (ImGui::SliderFloat("choppiness",   &chop,       0.0f, 2.5f))  ocean.setChoppiness(chop);
                float timeScale = ocean.getTimeScale();
                if (ImGui::SliderFloat("time scale",   &timeScale,  0.1f, 4.0f))  ocean.setTimeScale(timeScale);
            }
            ImGui::Checkbox("vehicles moving (P)", &vehiclesMoving);
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
            oceanMesh.draw(debugWireframeShader, camera.Position);
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
            uploadRipples(shader, activeRipples);
            oceanMesh.draw(shader, camera.Position);

            // --- OBJECTS (solid, depth-tested, before transparent skybox/rain) ---
            objectShader.use();
            objectShader.setMat4("projection", projection);
            objectShader.setMat4("view", view);
            objectShader.setVec3("viewPos", camera.Position);
            objectShader.setVec3("baseColor", glm::vec3(0.42f, 0.40f, 0.38f)); // grey rock
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
            rock.draw(objectShader);

            // --- COASTAL CLIFFS: real scanned cliff, ringed + facing inward ---
            for (const MountainInstance& m : mountains) {
                coastalCliff.setPosition(m.pos);
                coastalCliff.setScale(m.scale);
                coastalCliff.setRotationY(m.rotY);
                coastalCliff.draw(objectShader);
            }

            // --- VEHICLES: jet-ski / yacht / big-ship cruising the bay ---
            for (const Vehicle& v : vehicles) {
                v.model->setPosition(v.pos + glm::vec3(0.0f, v.yOffset, 0.0f));
                v.model->setScale(v.scale);
                v.model->setRotationY(v.heading + v.modelYaw);
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

            // Rain after skybox — blends correctly over sky and water
            rainSystem.render(rainShader, projection, view, camera.Position);
        }

        // Recording
        if (isRecording) {
            unsigned char* pixels        = new unsigned char[screenWidth * screenHeight * 3];
            unsigned char* flippedPixels = new unsigned char[screenWidth * screenHeight * 3];
            glReadPixels(0, 0, screenWidth, screenHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels);
            for (int y = 0; y < screenHeight; ++y) {
                memcpy(flippedPixels + (screenHeight - 1 - y) * screenWidth * 3,
                       pixels        + y * screenWidth * 3,
                       screenWidth * 3);
            }
            std::string filename = "frames/frame_" + std::to_string(frameCount) + ".png";
            stbi_write_png(filename.c_str(), screenWidth, screenHeight, 3, flippedPixels, screenWidth * 3);
            delete[] pixels;
            delete[] flippedPixels;

            if (++frameCount >= MAX_FRAMES) {
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
