#include "Shader.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

std::string Shader::loadFile(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to open shader file: ") + path);
    }

    std::stringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

unsigned int Shader::compileShader(GLenum type, const std::string& source, const char* label) {
    const char* shaderSource = source.c_str();
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &shaderSource, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());
        std::cerr << "ERROR::SHADER::COMPILATION_FAILED::" << label << "\n" << infoLog << std::endl;
    }

    return shader;
}

unsigned int Shader::linkProgram(const std::vector<unsigned int>& shaders, const char* label) {
    unsigned int program = glCreateProgram();
    for (unsigned int shader : shaders) {
        glAttachShader(program, shader);
    }

    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        int logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<std::size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, infoLog.data());
        std::cerr << "ERROR::SHADER::LINK_FAILED::" << label << "\n" << infoLog << std::endl;
    }

    for (unsigned int shader : shaders) {
        glDetachShader(program, shader);
        glDeleteShader(shader);
    }

    return program;
}

Shader::Shader(const char* vertexPath, const char* fragmentPath) {
    try {
        const std::string vertexCode = loadFile(vertexPath);
        const std::string fragmentCode = loadFile(fragmentPath);
        const unsigned int vertex = compileShader(GL_VERTEX_SHADER, vertexCode, vertexPath);
        const unsigned int fragment = compileShader(GL_FRAGMENT_SHADER, fragmentCode, fragmentPath);
        ID = linkProgram({ vertex, fragment }, vertexPath);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << std::endl;
        ID = 0;
    }
}

Shader::Shader(const char* computePath) {
    try {
        const std::string computeCode = loadFile(computePath);
        const unsigned int compute = compileShader(GL_COMPUTE_SHADER, computeCode, computePath);
        ID = linkProgram({ compute }, computePath);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << std::endl;
        ID = 0;
    }
}

Shader::~Shader() {
    if (ID != 0) {
        glDeleteProgram(ID);
    }
}

void Shader::use() const {
    glUseProgram(ID);
}

void Shader::setMat4(const std::string &name, const glm::mat4 &mat) const {
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, glm::value_ptr(mat));
}

void Shader::setVec2(const std::string &name, const glm::vec2 &value) const {
    glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, glm::value_ptr(value));
}

void Shader::setVec3(const std::string &name, const glm::vec3 &value) const {
    glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
}

void Shader::setVec3Array(const std::string &name, const glm::vec3* values, int count) const {
    if (count <= 0 || values == nullptr) {
        return;
    }

    glUniform3fv(glGetUniformLocation(ID, name.c_str()), count, glm::value_ptr(values[0]));
}

void Shader::setFloat(const std::string &name, float value) const {
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setInt(const std::string &name, int value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setUInt(const std::string &name, unsigned int value) const {
    glUniform1ui(glGetUniformLocation(ID, name.c_str()), value);
}
