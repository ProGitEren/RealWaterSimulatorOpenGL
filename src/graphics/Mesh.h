#ifndef MESH_H
#define MESH_H

#include "Shader.h"
#include <glm/glm.hpp>
#include <vector>

// A single drawable piece of geometry: interleaved position/normal/uv vertices
// plus an index buffer. Owns its GL objects (RAII) like OceanMesh does.
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Per-mesh material: GL texture ids (0 = absent) + a base-colour factor used
// when there is no albedo texture (e.g. the untextured yacht/ship parts).
// Bound by Model::draw before the mesh is drawn so object.frag can use them.
struct Material {
    unsigned int albedo    = 0;
    unsigned int normalMap = 0;
    unsigned int roughness = 0;
    unsigned int ao        = 0;
    glm::vec3    baseColor  = glm::vec3(0.8f); // factor used when albedo == 0
};

class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    ~Mesh();

    Material material;

    // Non-copyable (owns GL handles); movable so it can live in a std::vector.
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void draw() const;

private:
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    GLsizei      m_indexCount = 0;

    void release();
};

#endif
