#ifndef MODEL_H
#define MODEL_H

#include "Mesh.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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
    // Orientation. setRotationY is kept for existing callers (cliffs, vehicles);
    // it routes through the same quaternion so buoyancy pitch/roll and rolling-rock
    // spin can compose on top instead of a separate yaw float fighting them.
    void setRotationY(float radians)        { m_orientation = glm::angleAxis(radians, glm::vec3(0.0f, 1.0f, 0.0f)); }
    void setOrientation(const glm::quat& q) { m_orientation = q; }
    // Local-space AABB centre of the loaded mesh (0 if empty). Used to align the
    // boat wake (and other effects) to the VISIBLE hull centre when a model's
    // mesh sits off its glTF origin.
    glm::vec3 localCenter() const {
        return (m_aabbMax.x < m_aabbMin.x) ? glm::vec3(0.0f) : (m_aabbMin + m_aabbMax) * 0.5f;
    }

    glm::mat4 modelMatrix() const;

    // Sets the "model" uniform on the shader and draws every mesh.
    void draw(const Shader& shader) const;

private:
    std::vector<Mesh> m_meshes;

    glm::vec3 m_position    = glm::vec3(0.0f);
    glm::vec3 m_scale       = glm::vec3(1.0f);
    glm::quat m_orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity (w,x,y,z)
    glm::vec3 m_aabbMin     = glm::vec3( 1e9f);  // local-space mesh bounds (baked at load)
    glm::vec3 m_aabbMax     = glm::vec3(-1e9f);

    void loadFromFile(const std::string& path);
};

#endif
