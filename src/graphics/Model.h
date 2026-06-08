#ifndef MODEL_H
#define MODEL_H

#include "Mesh.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Loads a glTF/GLB file into one or more Meshes via cgltf, and carries a
// transform (position / rotation / uniform scale) used to build the model
// matrix. This is the reusable loader for any solid object: rocks, boats,
// ships, jet skis.
class Model {
public:
    // Loads from a .glb/.gltf path. On failure, leaves the model empty (no
    // meshes) and prints an error; check loaded().
    explicit Model(const std::string& path);

    // Builds a Model directly from in-memory geometry (used by the procedural
    // rock generator).
    Model(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);

    bool loaded() const { return !m_meshes.empty(); }

    // Transform
    void setPosition(const glm::vec3& p) { m_position = p; }
    void setScale(float s)               { m_scale = glm::vec3(s); }
    void setScale(const glm::vec3& s)    { m_scale = s; }          // non-uniform (stretch)
    void setRotationY(float radians)     { m_rotationY = radians; }
    glm::vec3 getPosition() const        { return m_position; }

    glm::mat4 modelMatrix() const;

    // Sets the "model" uniform on the shader and draws every mesh.
    void draw(const Shader& shader) const;

private:
    std::vector<Mesh> m_meshes;

    glm::vec3 m_position  = glm::vec3(0.0f);
    glm::vec3 m_scale     = glm::vec3(1.0f);
    float     m_rotationY = 0.0f;

    void loadFromFile(const std::string& path);
};

#endif
