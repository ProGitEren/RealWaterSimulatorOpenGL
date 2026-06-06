#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include <iostream>
#include <vector>

int main() {
    Window window(1024, 768, "Real-Time Water Simulator");
    if (!window.getGLFWWindow()) return -1;

    // Trap the mouse cursor inside the window for fly-cam
    glfwSetInputMode(window.getGLFWWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Initialize Camera and Shader
    Camera camera(glm::vec3(0.0f, 5.0f, 15.0f));
    Shader shader("../assets/shaders/standard.vert", "../assets/shaders/standard.frag");

    // --- GENERATE THE WATER GRID MESH ---
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    int gridSize = 50;       // 50x50 cells
    float tileSize = 1.0f;   // Size of each cell

    // 1. Generate Vertices (X, Y, Z)
    for (int z = 0; z <= gridSize; ++z) {
        for (int x = 0; x <= gridSize; ++x) {
            vertices.push_back(x * tileSize - (gridSize * tileSize) / 2.0f); // X
            vertices.push_back(0.0f);                                        // Y (Flat for now)
            vertices.push_back(z * tileSize - (gridSize * tileSize) / 2.0f); // Z
        }
    }

    // 2. Generate Indices (Two triangles per quad)
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

    // --- SEND MESH TO GPU (VAO, VBO, EBO) ---
    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Render the grid as wireframe so we can see the mesh!
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // --- RENDER LOOP VARIABLES ---
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    double lastX = 1024.0 / 2.0;
    double lastY = 768.0 / 2.0;
    bool firstMouse = true;

    glClearColor(0.05f, 0.05f, 0.1f, 1.0f); // Dark background
    glEnable(GL_DEPTH_TEST);

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

        // Mouse Look
        double xpos, ypos;
        glfwGetCursorPos(win, &xpos, &ypos);
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // Reversed since y-coordinates go from bottom to top
        lastX = xpos;
        lastY = ypos;
        camera.ProcessMouseMovement(xoffset, yoffset);

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

        // Draw the Grid
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

        window.swapBuffers();
        window.pollEvents();
    }

    return 0;
}
