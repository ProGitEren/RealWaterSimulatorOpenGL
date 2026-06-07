#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "ocean/GPUFFTOcean.h"
#include "ocean/GPUDisturbance.h"
#include "ocean/OceanMesh.h"
#include "water/WaterSimulation.h"
#include "water/RainSystem.h"

#include <iostream>
#include <iomanip>
#include <deque>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

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

    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    Camera camera(glm::vec3(0.0f, 40.0f, 120.0f));

    Shader shader("../assets/shaders/standard.vert", "../assets/shaders/standard.frag");
    Shader debugWireframeShader("../assets/shaders/standard.vert", "../assets/shaders/debug_wireframe.frag");
    Shader rainShader("../assets/shaders/rain.vert", "../assets/shaders/rain.frag");
    Shader skyboxShader("../assets/shaders/skybox.vert", "../assets/shaders/skybox.frag");

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

        // Camera movement
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(0, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(1, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(2, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(3, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_SPACE)      == GLFW_PRESS) camera.ProcessKeyboard(4, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.ProcessKeyboard(5, deltaTime);

        // C key — fire a GPU wave disturbance 40m in front of camera (debounced)
        const bool cKeyPressed = glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS;
        if (cKeyPressed && !cKeyWasPressed) {
            glm::vec3 flatFront = glm::normalize(glm::vec3(camera.Front.x, 0.0f, camera.Front.z));
            glm::vec2 impactXZ  = glm::vec2(camera.Position.x, camera.Position.z)
                                + glm::vec2(flatFront.x, flatFront.z) * 40.0f;
            disturbance.disturb(impactXZ, 3.0f);
        }
        cKeyWasPressed = cKeyPressed;

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
            isRecording = true;
            frameCount = 0;
        }

        // Mouse look
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
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
            accumulator -= kFixedDt;
        }

        // --- RENDER ---
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            static_cast<float>(window.getWidth()) / static_cast<float>(window.getHeight()),
            0.1f, 2000.0f
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

        window.swapBuffers();
        window.pollEvents();
    }

    glDeleteBuffers(1, &skyboxVBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteTextures(1, &cubemapTexture);

    return 0;
}
