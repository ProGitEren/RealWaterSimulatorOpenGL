#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Shader {
public:
    unsigned int ID;

    Shader(const char* vertexPath, const char* fragmentPath);
    explicit Shader(const char* computePath);
    ~Shader();

    void use() const;

    void setMat4(const std::string &name, const glm::mat4 &mat) const;
    void setVec2(const std::string &name, const glm::vec2 &value) const;
    void setVec3(const std::string &name, const glm::vec3 &value) const;
    void setFloat(const std::string &name, float value) const;
    void setInt(const std::string &name, int value) const;
    void setUInt(const std::string &name, unsigned int value) const;

private:
    static std::string loadFile(const char* path);
    static unsigned int compileShader(GLenum type, const std::string& source, const char* label);
    static unsigned int linkProgram(const std::vector<unsigned int>& shaders, const char* label);
};

#endif
