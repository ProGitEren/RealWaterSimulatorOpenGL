#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

int main() {
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(64, 64, "glcheck", nullptr, nullptr);
    if (!window) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return 2;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "gladLoadGLLoader failed\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 3;
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    std::cout << "GL_VERSION=" << (version ? version : "null") << "\n";
    std::cout << "GL_RENDERER=" << (renderer ? renderer : "null") << "\n";
    std::cout << "GL_VENDOR=" << (vendor ? vendor : "null") << "\n";
    std::cout << "GLSL_VERSION=" << (glsl ? glsl : "null") << "\n";
    std::cout << "GLVersionStruct=" << GLVersion.major << "." << GLVersion.minor << "\n";

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
