#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "water/WaterSimulation.h" 
#include <iostream>
#include <vector>
#include <string>
#include "water/RainSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- ADD STB IMAGE WRITE ---
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"


unsigned int loadCubemap(std::vector<std::string> faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                         0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
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


int main() {
    Window window(1024, 768, "Real-Time Water Simulator");
    if (!window.getGLFWWindow()) return -1;

    // Trap the mouse cursor inside the window for fly-cam
    // glfwSetInputMode(window.getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetInputMode(window.getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Initialize Camera and Shader
    Camera camera(glm::vec3(0.0f, 15.0f, 25.0f)); 
    Shader shader("../assets/shaders/standard.vert", "../assets/shaders/standard.frag");
    Shader rainShader("../assets/shaders/rain.vert", "../assets/shaders/rain.frag");
    Shader skyboxShader("../assets/shaders/skybox.vert", "../assets/shaders/skybox.frag");
  
    // --- SYKBOX SETUP ---
    float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f
    };

    unsigned int skyboxVAO, skyboxVBO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Load the 6 textures
    std::vector<std::string> faces = {
        "../assets/textures/skybox/right.png",
        "../assets/textures/skybox/left.png",
        "../assets/textures/skybox/top.png",
        "../assets/textures/skybox/bottom.png",
        "../assets/textures/skybox/front.png",
        "../assets/textures/skybox/back.png"
    };
    unsigned int cubemapTexture = loadCubemap(faces);

 

    // --- GENERATE THE WATER GRID MESH ---
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    int gridSize = 150;       // 50x50 cells
    float tileSize = 1.0f;   // Size of each cell
    float floorY = -10.0f; // The pyhsical depth of our 3D box

    // Initialize our physics engine
    // gridSize=50, spatialStep=1.0, timeStep=0.016 (60fps), waveSpeed=10.0, damping=0.99
    WaterSimulation water(gridSize, tileSize, 0.016f, 8.0f, 0.995f);

    // Initialize the rain system
    RainSystem rainSystem(gridSize, tileSize);

    int numSurfaceVertices = (gridSize + 1) * (gridSize + 1);

    // 1. Generate Surface Vertices (X, Y, Z) (Dynamically moving)
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            vertices.push_back(x * tileSize - (gridSize * tileSize) / 2.0f); // X
            vertices.push_back(0.0f);                                        // Y (Flat for now)
            vertices.push_back(z * tileSize - (gridSize * tileSize) / 2.0f); // Z
            vertices.push_back(0.0f); // Normal X
            vertices.push_back(1.0f); // Normal Y
            vertices.push_back(0.0f); // Normal Z
            // Default Normal pointing up 
        }
    }

    // 2. Generate FLOOR  Vertices (Static, permanent depth)
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            vertices.push_back(x * tileSize - (gridSize * tileSize) / 2.0f);
            vertices.push_back(floorY); 
            vertices.push_back(z * tileSize - (gridSize * tileSize) / 2.0f);
            vertices.push_back(0.0f); // Normal X
            vertices.push_back(1.0f); // Normal Y
            vertices.push_back(0.0f); // Normal Z
            // Default Normal pointing up 
        }
    }

    // 3. Generate Indices (Two triangles per quad) (For Waving Surface)
    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            int topLeft = (z * (gridSize + 1)) + x;
            int topRight = topLeft + 1;
            int bottomLeft = ((z + 1) * (gridSize + 1)) + x;
            int bottomRight = bottomLeft + 1;

            // Triangle 1
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            // Triangle 2
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    // 4. Generate Indices for the Side Walls 
    int btmOffset = numSurfaceVertices;

    // Helper Lambda to draw a wall quad
    auto addWallQuad = [&](int t1, int t2, int b1, int b2) {
        indices.push_back(t1); indices.push_back(b1); indices.push_back(t2);
        indices.push_back(t2); indices.push_back(b1); indices.push_back(b2);
    };

    for (int i = 0; i < gridSize; ++i) {

        // Front Wall (z = 0)
        addWallQuad(i, i + 1, btmOffset + i, btmOffset + i + 1);

        // Back Wall (z = gridSize)
        int backZ = gridSize * (gridSize + 1);
        addWallQuad(backZ + i + 1, backZ + i, btmOffset + backZ + i + 1, btmOffset + backZ + i);

        // Left Wall (x = 0)
        int leftX1 = i * (gridSize + 1);
        int leftX2 = (i + 1) * (gridSize + 1);
        addWallQuad(leftX2, leftX1, btmOffset + leftX2, btmOffset + leftX1);
        
        // Right Wall (x = gridSize)
        int rightX1 = i * (gridSize + 1) + gridSize;
        int rightX2 = (i + 1) * (gridSize + 1) + gridSize;
        addWallQuad(rightX1, rightX2, btmOffset + rightX1, btmOffset + rightX2);


    }

    // 5. Generate Indices for Bottom Face (Closing the box completely)
    int tL = btmOffset;
    int tR = btmOffset + gridSize;
    int bL = btmOffset + gridSize * (gridSize + 1);
    int bR = btmOffset + gridSize * (gridSize + 1) + gridSize;
    
    indices.push_back(tL); indices.push_back(tR); indices.push_back(bL);
    indices.push_back(tR); indices.push_back(bR); indices.push_back(bL);

    // --- SEND MESH TO GPU (VAO, VBO, EBO) ---
    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1); 

    // [PHASE 3 UPDATE] The glPolygonMode line was deleted from here so the mesh is solid!

    // --- RENDER LOOP VARIABLES ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double lastX = 1024.0 / 2.0;
    double lastY = 768.0 / 2.0;
    bool firstMouse = true;
    
    float dropCooldown = 0.0f; 

    // --- RECORDING VARIABLES ---
    bool isRecording = false;
    int frameCount = 0;
    const int MAX_FRAMES = 600; // 10 seconds of video at 60fps
    int screenWidth = 1024;
    int screenHeight = 768;


    glClearColor(0.05f, 0.05f, 0.1f, 1.0f); // Dark background
    glEnable(GL_DEPTH_TEST);

    // --- [PHASE 3 UPDATE] ADD WATER TRANSPARENCY ---
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    while (!window.shouldClose()) {
        // Calculate Delta Time (for smooth movement)
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // --- INPUT HANDLING ---
        GLFWwindow* win = window.getGLFWWindow();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(win, true);
        
        // Keyboard movement
        // ---> CHANGE THIS BLOCK RIGHT HERE <---
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) {
            camera.ProcessKeyboard(0, deltaTime);
            //std::cout << "W Key Pressed! Velocity: " << (10.0f * deltaTime) << std::endl;
        }
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(1, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(2, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(3, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(4, deltaTime); // UP
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.ProcessKeyboard(5, deltaTime); // DOWN

        // 3. Soften the splash!
        if (glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS && dropCooldown <= 0.0f) {
            // Drop a smaller weight, and maybe disturb a few vertices next to each other
            // instead of just one point to make a wider, softer wave.
            water.disturb(gridSize / 2, gridSize / 2, 0.5f);
            dropCooldown = 0.0f; 
        }

        // Press 'R' to start recording exactly 600 frames!
        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS && !isRecording) {
            std::cout << "RECORDING STARTED! Simulation will lag, this is normal..." << std::endl;
            isRecording = true;
            frameCount = 0;
            // Spawn a wave automatically when recording starts so you have something cool to record!
            // water.disturb(gridSize / 2, gridSize / 2, 15.0f); 
        }

        // Mouse Look
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // Reversed since y-coordinates go from bottom to top
        lastX = xpos;
        lastY = ypos;
        camera.ProcessMouseMovement(xoffset, yoffset);

  
        // // Uniform Wave Disturbance
        // // Generate a rolling wave on the X=0 edge of the grid
        // float time = glfwGetTime();
        // for (int z = 0; z < gridSize; ++z) {
        //     // The sin() function creates a smooth up-and-down motion.
        //     // Tweak the 2.0f (speed) and 0.5f (size) to get the perfect ocean swell!
        //     float swellHeight = sin(time * 2.0f + z * 0.2f) * 0.2f; 
        //     water.disturb(2, z, swellHeight); 
        // }

        // --- RANDOM RAIN SIMULATOR ---
        // int dropsPerFrame = 10; 
        // for (int i = 0; i < dropsPerFrame; ++i) {
        //     int randomX = 1 + (rand() % (gridSize - 1));
        //     int randomZ = 1 + (rand() % (gridSize - 1));
            
        //     // Randomize the drop size between 0.5f and 1.5f
        //     float dropSize = 0.2f + (rand() % 15) / 200.0f;
        //     water.disturbRain(randomX, randomZ, dropSize); 
        // }


        // --- RUN PHYSICS ---
        water.update();

        // --- UPDATE RAIN ---
        rainSystem.update(deltaTime, water);

        // --- UPDATE CPU MESH ---
        float time = glfwGetTime();

        // Helper lambda to safely get the total combined height of any X, Z point
        auto getTotalHeight = [&](int hX, int hZ) {
            // Clamp so we don't crash if we check outside the array bounds
            int safeX = std::max(0, std::min(hX, gridSize));
            int safeZ = std::max(0, std::min(hZ, gridSize));
            
            float pHeight = water.getHeight(safeX, safeZ);
            //float w1 = sin(safeX * 1.5f + time * 2.0f) * 0.25;
            //float w2 = cos(safeZ * 1.8f + time * 2.0f) * 0.25f;
            return pHeight; // + w1 + w2;
        };
        
        for (int z = 0; z <= gridSize; ++z) {
            for (int x = 0; x <= gridSize; ++x) {
                // STRIDE IS NOW 6! (X, Y, Z, NX, NY, NZ)
                int vertexIndex = (z * (gridSize + 1) + x) * 6;

                // 1. Set the new Y height
                vertices[vertexIndex + 1] = getTotalHeight(x, z);

                // 2. Central Difference Normal Calculation
                float L = getTotalHeight(x - 1, z); // Left
                float R = getTotalHeight(x + 1, z); // Right
                float D = getTotalHeight(x, z - 1); // Down (Back)
                float U = getTotalHeight(x, z + 1); // Up (Forward)

                // Calculate the slope
                glm::vec3 normal = glm::normalize(glm::vec3(L - R, 2.0f * tileSize, D - U));

                // 3. Inject the new normal into the array
                vertices[vertexIndex + 3] = normal.x;
                vertices[vertexIndex + 4] = normal.y;
                vertices[vertexIndex + 5] = normal.z;
            }
        }

        // --- SEND *ONLY* SURFACE TO GPU ---
        // We only send the top half of the vertices array to the GPU! The walls will stretch automatically.
        int surfaceByteSize = numSurfaceVertices * 6 * sizeof(float);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, surfaceByteSize, vertices.data());

        // --- RENDERING ---
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();

        // Matrices
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)window.getWidth() / (float)window.getHeight(), 0.1f, 100.0f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 model = glm::mat4(1.0f);

        shader.setMat4("projection", projection);
        shader.setMat4("view", view);
        shader.setMat4("model", model);

        // Send Camera Position for Lightning
        shader.setVec3("viewPos", camera.Position);

        // For setting a ground floor
        shader.setFloat("floorY", -15.0f); // Set the pool floor 15 units down
        shader.setFloat("time", time);

        // --- SEND RIPPLES TO FRAGMENT SHADER ---
        shader.setInt("numRipples", rainSystem.m_activeRipples.size());
        for(int i = 0; i < rainSystem.m_activeRipples.size(); i++) {
            std::string arrayName = "activeRipples[" + std::to_string(i) + "]";
            shader.setVec3(arrayName, rainSystem.m_activeRipples[i]);
        }

        // Draw the Grid
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

        // Draw the Rain
        rainSystem.render(rainShader, projection, view);

        // Draw the Skybox (Keep this at the very end, after drawing water and rain!)
        
        // FIX 1: Change Depth Function to LEQUAL (Less than or equal to 1.0)
        glDepthFunc(GL_LEQUAL); 
        
        skyboxShader.use();
        
        // Remove translation from view matrix
        glm::mat4 skyView = glm::mat4(glm::mat3(camera.GetViewMatrix())); 
        skyboxShader.setMat4("view", skyView);
        skyboxShader.setMat4("projection", projection);

        glBindVertexArray(skyboxVAO);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        
        // FIX 2: Reset Depth Function back to default for the next frame
        glDepthFunc(GL_LESS);

        // --- RECORDING LOGIC (Must happen right before swapBuffers) ---
        if (isRecording) {
            unsigned char* pixels = new unsigned char[screenWidth * screenHeight * 3];
            glReadPixels(0, 0, screenWidth, screenHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels);

            // Flip image vertically (OpenGL is bottom-to-top, PNG is top-to-bottom)
            unsigned char* flippedPixels = new unsigned char[screenWidth * screenHeight * 3];
            for (int y = 0; y < screenHeight; ++y) {
                memcpy(flippedPixels + (screenHeight - 1 - y) * screenWidth * 3, 
                       pixels + y * screenWidth * 3, screenWidth * 3);
            }

            // Save to the "frames" folder
            std::string filename = "frames/frame_" + std::to_string(frameCount) + ".png";
            stbi_write_png(filename.c_str(), screenWidth, screenHeight, 3, flippedPixels, screenWidth * 3);
            
            std::cout << "Saved " << filename << " (" << frameCount + 1 << "/" << MAX_FRAMES << ")" << std::endl;

            delete[] pixels;
            delete[] flippedPixels;

            frameCount++;
            if (frameCount >= MAX_FRAMES) {
                isRecording = false;
                std::cout << "RECORDING FINISHED! You can close the app now." << std::endl;
            }
        }

        window.swapBuffers();
        window.pollEvents();
    }

    return 0;
}
