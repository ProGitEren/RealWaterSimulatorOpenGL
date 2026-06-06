#include "core/Window.h"
#include <iostream>

int main() {
    // Create an 800x600 window
    Window window(800, 600, "Real-Time Water Simulator");

    if (!window.getGLFWWindow()) {
        return -1; // Window creation failed
    }

    // Set a cool deep water blue clear color
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);

    // --- MAIN RENDER LOOP ---
    while (!window.shouldClose()) {
        // Input checking (we'll add an InputHandler later)
        if (glfwGetKey(window.getGLFWWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window.getGLFWWindow(), true);
        }

        // Render
        glClear(GL_COLOR_BUFFER_BIT);

        // ... Water drawing code will go here eventually ...

        // Swap buffers and poll IO events
        window.swapBuffers();
        window.pollEvents();
    }

    return 0;
}
