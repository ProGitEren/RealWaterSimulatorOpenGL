#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "water/WaterSimulation.h" 
#include <iostream>
#include <vector>
#include <string>
#include "water/RainSystem.h"

// --- ADD STB IMAGE WRITE ---
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

int main() {
    Window window(1024, 768, "Real-Time Water Simulator");
    if (!window.getGLFWWindow()) return -1;

    // Trap the mouse cursor inside the window for fly-cam
    glfwSetInputMode(window.getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize Camera and Shader
    Camera camera(glm::vec3(0.0f, 15.0f, 25.0f)); 
    Shader shader("../assets/shaders/standard.vert", "../assets/shaders/standard.frag");
    Shader rainShader("../assets/shaders/rain.vert", "../assets/shaders/rain.frag");

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
        }
    }

    // 2. Generate FLOOR  Vertices (Static, permanent depth)
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            vertices.push_back(x * tileSize - (gridSize * tileSize) / 2.0f);
            vertices.push_back(floorY); 
            vertices.push_back(z * tileSize - (gridSize * tileSize) / 2.0f);
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

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

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
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(0, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(1, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(2, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(3, deltaTime);
        if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(4, deltaTime); // UP
        if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.ProcessKeyboard(5, deltaTime); // DOWN

        // 3. Soften the splash!
        if (glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS && dropCooldown <= 0.0f) {
            // Drop a smaller weight, and maybe disturb a few vertices next to each other
            // instead of just one point to make a wider, softer wave.
            water.disturb(gridSize / 2, gridSize / 2, 10.0f); 
            water.disturb(gridSize / 2 + 1, gridSize / 2, 8.0f);
            water.disturb(gridSize / 2 - 1, gridSize / 2, 8.0f);
            dropCooldown = 0.5f; 
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
        for (int z = 0; z <= gridSize; ++z) {
            for (int x = 0; x <= gridSize; ++x) {
                int vertexIndex = (z * (gridSize + 1) + x) * 3;

                // 1. Get the physics height (This will be 0.0 unless you press 'C')
                float physicsHeight = water.getHeight(x, z);

                // 2. Calculate the Ambient Ocean Wave (Pure Math!)
                // By adding a sine wave on the X axis and a cosine wave on the Z axis,
                // you get a beautiful, rolling, diagonal ocean swell everywhere at once.
                // You can tweak the 0.5f (height) and 1.5f/2.0f (speed) to make it stormier!
                float wave1 = sin(x * 1.5f + time * 2.0f) * 0.25f;
                float wave2 = cos(z * 1.8f + time * 2.0f) * 0.25f;
                float oceanHeight = wave1 + wave2;

                // 3. Combine them together!
                vertices[vertexIndex + 1] = physicsHeight + oceanHeight;
            }
        }

        // --- SEND *ONLY* SURFACE TO GPU ---
        // We only send the top half of the vertices array to the GPU! The walls will stretch automatically.
        int surfaceByteSize = numSurfaceVertices * 3 * sizeof(float);
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

        // For setting a ground floor
        shader.setFloat("floorY", -15.0f); // Set the pool floor 15 units down

        // Draw the Grid
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

        // Draw the Rain
        rainSystem.render(rainShader, projection, view);

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
