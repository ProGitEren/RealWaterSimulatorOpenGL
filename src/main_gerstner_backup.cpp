#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "water/WaterSimulation.h"
#include "water/RainSystem.h"

#include <iostream>
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

    // For cubemap faces, keep flipping OFF
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
                0,
                format,
                width,
                height,
                0,
                format,
                GL_UNSIGNED_BYTE,
                data
            );

            std::cout << "Loaded cubemap face: " << faces[i] << std::endl;
            stbi_image_free(data);
        } else {
            std::cout << "Cubemap tex failed to load at path: " << faces[i] << std::endl;
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
    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr float kMaxAccumulatedTime = 0.25f;
    constexpr int kSurfaceChunkSize = 16;
    constexpr int kMaxLocalRipples = 128;
    constexpr float kRippleExpansionSpeed = 1.5f;
    constexpr float kRippleBandWidth = 0.2f;

    struct SurfaceChunk {
        unsigned int ebo = 0;
        GLsizei indexCount = 0;
        glm::vec2 minXZ = glm::vec2(0.0f);
        glm::vec2 maxXZ = glm::vec2(0.0f);
    };

    bool rippleAffectsChunk(const glm::vec3& ripple, const SurfaceChunk& chunk) {
        const float influenceRadius = ripple.z * kRippleExpansionSpeed + kRippleBandWidth;
        const float clampedX = std::max(chunk.minXZ.x, std::min(ripple.x, chunk.maxXZ.x));
        const float clampedZ = std::max(chunk.minXZ.y, std::min(ripple.y, chunk.maxXZ.y));
        const float dx = ripple.x - clampedX;
        const float dz = ripple.y - clampedZ;
        return (dx * dx + dz * dz) <= (influenceRadius * influenceRadius);
    }

    void uploadRipples(const Shader& shader, const std::vector<glm::vec3>& ripples) {
        shader.setInt("numRipples", static_cast<int>(ripples.size()));
        if (!ripples.empty()) {
            shader.setVec3Array("activeRipples", ripples.data(), static_cast<int>(ripples.size()));
        }
    }
}


int main() {
    Window window(1024, 768, "Real-Time Water Simulator");
    if (!window.getGLFWWindow()) return -1;

    GLFWwindow* win = window.getGLFWWindow();

    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Camera and shaders
    Camera camera(glm::vec3(0.0f, 15.0f, 25.0f));

    // IMPORTANT:
    // These paths assume your exe runs from /build and assets are copied into /build/assets
    Shader shader("../assets/shaders/standard.vert", "../assets/shaders/standard.frag");
    Shader debugWireframeShader("../assets/shaders/standard.vert", "../assets/shaders/debug_wireframe.frag");
    Shader rainShader("../assets/shaders/rain.vert", "../assets/shaders/rain.frag");
    Shader skyboxShader("../assets/shaders/skybox.vert", "../assets/shaders/skybox.frag");

    skyboxShader.use();
    skyboxShader.setInt("skybox", 0);

    // --- SKYBOX SETUP ---
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
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

    // --- WATER GRID SETUP ---
    std::vector<float> vertices;
    std::vector<unsigned int> staticIndices;
    std::vector<SurfaceChunk> surfaceChunks;

    int gridSize = 150;
    float tileSize = 1.0f;
    float floorY = -10.0f;
    const float halfGridWorldSize = (gridSize * tileSize) / 2.0f;

    WaterSimulation water(gridSize, tileSize, kFixedDt, 8.0f, 0.998f);
    RainSystem rainSystem(gridSize, tileSize);

    int numSurfaceVertices = (gridSize + 1) * (gridSize + 1);

    // Surface vertices
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            vertices.push_back(x * tileSize - halfGridWorldSize);            // X
            vertices.push_back(0.0f);                                         // Y
            vertices.push_back(z * tileSize - halfGridWorldSize);            // Z
            vertices.push_back(0.0f); // NX
            vertices.push_back(1.0f); // NY
            vertices.push_back(0.0f); // NZ
        }
    }

    // Bottom vertices
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            vertices.push_back(x * tileSize - halfGridWorldSize);
            vertices.push_back(floorY);
            vertices.push_back(z * tileSize - halfGridWorldSize);
            vertices.push_back(0.0f);
            vertices.push_back(1.0f);
            vertices.push_back(0.0f);
        }
    }

    // Surface chunk indices
    for (int chunkZ = 0; chunkZ < gridSize; chunkZ += kSurfaceChunkSize) {
        for (int chunkX = 0; chunkX < gridSize; chunkX += kSurfaceChunkSize) {
            SurfaceChunk chunk;
            std::vector<unsigned int> chunkIndices;

            int chunkEndZ = std::min(chunkZ + kSurfaceChunkSize, gridSize);
            int chunkEndX = std::min(chunkX + kSurfaceChunkSize, gridSize);
            chunk.minXZ = glm::vec2(chunkX * tileSize - halfGridWorldSize, chunkZ * tileSize - halfGridWorldSize);
            chunk.maxXZ = glm::vec2(chunkEndX * tileSize - halfGridWorldSize, chunkEndZ * tileSize - halfGridWorldSize);
            chunkIndices.reserve((chunkEndZ - chunkZ) * (chunkEndX - chunkX) * 6);

            for (int z = chunkZ; z < chunkEndZ; ++z) {
                for (int x = chunkX; x < chunkEndX; ++x) {
                    int topLeft = (z * (gridSize + 1)) + x;
                    int topRight = topLeft + 1;
                    int bottomLeft = ((z + 1) * (gridSize + 1)) + x;
                    int bottomRight = bottomLeft + 1;

                    chunkIndices.push_back(topLeft);
                    chunkIndices.push_back(bottomLeft);
                    chunkIndices.push_back(topRight);

                    chunkIndices.push_back(topRight);
                    chunkIndices.push_back(bottomLeft);
                    chunkIndices.push_back(bottomRight);
                }
            }

            glGenBuffers(1, &chunk.ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunkIndices.size() * sizeof(unsigned int), chunkIndices.data(), GL_STATIC_DRAW);
            chunk.indexCount = static_cast<GLsizei>(chunkIndices.size());
            surfaceChunks.push_back(chunk);
        }
    }

    // Side walls
    int btmOffset = numSurfaceVertices;

    auto addWallQuad = [&](int t1, int t2, int b1, int b2) {
        staticIndices.push_back(t1); staticIndices.push_back(b1); staticIndices.push_back(t2);
        staticIndices.push_back(t2); staticIndices.push_back(b1); staticIndices.push_back(b2);
    };

    for (int i = 0; i < gridSize; ++i) {
        // Front
        addWallQuad(i, i + 1, btmOffset + i, btmOffset + i + 1);

        // Back
        int backZ = gridSize * (gridSize + 1);
        addWallQuad(backZ + i + 1, backZ + i, btmOffset + backZ + i + 1, btmOffset + backZ + i);

        // Left
        int leftX1 = i * (gridSize + 1);
        int leftX2 = (i + 1) * (gridSize + 1);
        addWallQuad(leftX2, leftX1, btmOffset + leftX2, btmOffset + leftX1);

        // Right
        int rightX1 = i * (gridSize + 1) + gridSize;
        int rightX2 = (i + 1) * (gridSize + 1) + gridSize;
        addWallQuad(rightX1, rightX2, btmOffset + rightX1, btmOffset + rightX2);
    }

    // Bottom face
    int tL = btmOffset;
    int tR = btmOffset + gridSize;
    int bL = btmOffset + gridSize * (gridSize + 1);
    int bR = btmOffset + gridSize * (gridSize + 1) + gridSize;

    staticIndices.push_back(tL); staticIndices.push_back(tR); staticIndices.push_back(bL);
    staticIndices.push_back(tR); staticIndices.push_back(bR); staticIndices.push_back(bL);

    unsigned int VAO, VBO, staticEBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &staticEBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, staticIndices.size() * sizeof(unsigned int), staticIndices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // --- LOOP VARIABLES ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double lastX = 1024.0 / 2.0;
    double lastY = 768.0 / 2.0;
    bool firstMouse = true;

    bool initialSplashDone = false;
    bool wireframeMode = false;
    bool vKeyWasPressed = false;

    bool isRecording = false;
    int frameCount = 0;
    const int MAX_FRAMES = 600;
    int screenWidth = 1024;
    int screenHeight = 768;

    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!window.shouldClose()) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(win, true);

        const bool vKeyPressed = glfwGetKey(win, GLFW_KEY_V) == GLFW_PRESS;
        if (vKeyPressed && !vKeyWasPressed) {
            wireframeMode = !wireframeMode;
            std::cout << "Render mode: " << (wireframeMode ? "wireframe mesh" : "normal shaded") << std::endl;
        }
        vKeyWasPressed = vKeyPressed;

        // Camera movement
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(0, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(1, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(2, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(3, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(4, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.ProcessKeyboard(5, deltaTime);

        // 
        if (glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS) {
             water.disturb(gridSize / 2, gridSize / 2, 0.1f);

        }

        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS && !isRecording) {
            std::cout << "RECORDING STARTED!" << std::endl;
            isRecording = true;
            frameCount = 0;
        }

        // Mouse look
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = static_cast<float>(xpos - lastX);
        float yoffset = static_cast<float>(lastY - ypos);
        lastX = xpos;
        lastY = ypos;
        camera.ProcessMouseMovement(xoffset, yoffset);

        // Physics
        static float accumulator = 0.0f;
        accumulator = std::min(accumulator + deltaTime, kMaxAccumulatedTime);

        while (accumulator >= kFixedDt) {
            water.update();
            rainSystem.update(kFixedDt, water);
            accumulator -= kFixedDt;
        }
        
        // water.update();
        // rainSystem.update(deltaTime, water);

        float time = glfwGetTime();

        auto getTotalHeight = [&](int hX, int hZ) {
            int safeX = std::max(0, std::min(hX, gridSize));
            int safeZ = std::max(0, std::min(hZ, gridSize));
            return water.getHeight(safeX, safeZ);
        };

        for (int z = 0; z <= gridSize; ++z) {
            for (int x = 0; x <= gridSize; ++x) {
                int vertexIndex = (z * (gridSize + 1) + x) * 6;

                vertices[vertexIndex + 1] = getTotalHeight(x, z);

                float L = getTotalHeight(x - 1, z);
                float R = getTotalHeight(x + 1, z);
                float D = getTotalHeight(x, z - 1);
                float U = getTotalHeight(x, z + 1);

                glm::vec3 normal = glm::normalize(glm::vec3(L - R, 2.0f * tileSize, D - U));

                vertices[vertexIndex + 3] = normal.x;
                vertices[vertexIndex + 4] = normal.y;
                vertices[vertexIndex + 5] = normal.z;
            }
        }

        int surfaceByteSize = numSurfaceVertices * 6 * sizeof(float);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, surfaceByteSize, vertices.data());

        // Render
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(
            glm::radians(45.0f),
            (float)window.getWidth() / (float)window.getHeight(),
            0.1f,
            1000.0f
        );
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 model = glm::mat4(1.0f);

        if (wireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

            debugWireframeShader.use();
            debugWireframeShader.setMat4("projection", projection);
            debugWireframeShader.setMat4("view", view);
            debugWireframeShader.setMat4("model", model);
            debugWireframeShader.setFloat("time", time);

            glBindVertexArray(VAO);
            for (const SurfaceChunk& chunk : surfaceChunks) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebo);
                glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, 0);
            }
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticEBO);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(staticIndices.size()), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        } else {
            shader.use();
            shader.setMat4("projection", projection);
            shader.setMat4("view", view);
            shader.setMat4("model", model);
            shader.setVec3("viewPos", camera.Position);
            shader.setFloat("floorY", -15.0f);
            shader.setFloat("time", time);

            std::vector<glm::vec3> localRipples;
            localRipples.reserve(kMaxLocalRipples);

            glBindVertexArray(VAO);

            const std::vector<glm::vec3>& activeRipples = rainSystem.getActiveRipples();
            for (const SurfaceChunk& chunk : surfaceChunks) {
                localRipples.clear();

                for (const glm::vec3& ripple : activeRipples) {
                    if (rippleAffectsChunk(ripple, chunk)) {
                        localRipples.push_back(ripple);
                        if (localRipples.size() >= kMaxLocalRipples) {
                            break;
                        }
                    }
                }

                uploadRipples(shader, localRipples);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebo);
                glDrawElements(GL_TRIANGLES, chunk.indexCount, GL_UNSIGNED_INT, 0);
            }

            shader.setInt("numRipples", 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticEBO);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(staticIndices.size()), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        if (!wireframeMode) {
            // Rain
            rainSystem.render(rainShader, projection, view);

            // Skybox last
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);

            skyboxShader.use();
            glm::mat4 skyView = glm::mat4(glm::mat3(camera.GetViewMatrix()));
            skyboxShader.setMat4("view", skyView);
            skyboxShader.setMat4("projection", projection);

            glBindVertexArray(skyboxVAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);

            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
        }

        // Recording
        if (isRecording) {
            unsigned char* pixels = new unsigned char[screenWidth * screenHeight * 3];
            glReadPixels(0, 0, screenWidth, screenHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels);

            unsigned char* flippedPixels = new unsigned char[screenWidth * screenHeight * 3];
            for (int y = 0; y < screenHeight; ++y) {
                memcpy(
                    flippedPixels + (screenHeight - 1 - y) * screenWidth * 3,
                    pixels + y * screenWidth * 3,
                    screenWidth * 3
                );
            }

            std::string filename = "frames/frame_" + std::to_string(frameCount) + ".png";
            stbi_write_png(filename.c_str(), screenWidth, screenHeight, 3, flippedPixels, screenWidth * 3);

            delete[] pixels;
            delete[] flippedPixels;

            frameCount++;
            if (frameCount >= MAX_FRAMES) {
                isRecording = false;
                std::cout << "RECORDING FINISHED!" << std::endl;
            }
        }

        window.swapBuffers();
        window.pollEvents();
    }

    for (SurfaceChunk& chunk : surfaceChunks) {
        if (chunk.ebo != 0) {
            glDeleteBuffers(1, &chunk.ebo);
        }
    }
    glDeleteBuffers(1, &staticEBO);
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &skyboxVBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteTextures(1, &cubemapTexture);

    return 0;
}
