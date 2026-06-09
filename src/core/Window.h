#ifndef WINDOW_H
#define WINDOW_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    // Owns a raw GLFWwindow* (the dtor destroys it + terminates GLFW), so the
    // window is non-copyable and non-movable; it is constructed exactly once.
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    bool shouldClose() const;
    void swapBuffers() const;
    void pollEvents() const;
    
    // Getters
    GLFWwindow* getGLFWWindow() const { return m_window; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    GLFWwindow* m_window;
    int m_width;
    int m_height;

    // Callback for resizing the window
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
};

#endif
