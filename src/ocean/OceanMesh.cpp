#include "OceanMesh.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

OceanMesh::OceanMesh(int resolution, float tileSize)
    : m_vao(0), m_vbo(0), m_ebo(0), m_indexCount(0),
      m_resolution(resolution), m_tileSize(tileSize) {

    const int verts = resolution + 1;
    const float half = resolution * tileSize * 0.5f;

    std::vector<float> positions;
    positions.reserve(verts * verts * 3);
    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            positions.push_back(x * tileSize - half);
            positions.push_back(0.0f);
            positions.push_back(z * tileSize - half);
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(resolution * resolution * 6);
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            unsigned int tl = z * verts + x;
            unsigned int tr = tl + 1;
            unsigned int bl = (z + 1) * verts + x;
            unsigned int br = bl + 1;
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }

    m_indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(float), positions.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Only position (location 0) — no normals stored, shader gets (0,1,0) via glVertexAttrib3f
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

OceanMesh::~OceanMesh() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
}

void OceanMesh::draw(const Shader& shader, const glm::vec3& cameraPos) const {
    // Fixed ocean: the water grid stays centred at the world origin (it does NOT
    // follow the camera). This bounds the sea to the scene so it sits correctly
    // inside the ring of coastal cliffs instead of sliding underneath them.
    (void)cameraPos;
    glm::mat4 model = glm::mat4(1.0f);
    shader.setMat4("model", model);

    // aNormal (location 1) is not in the VBO; provide a flat up-normal as the default
    glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
