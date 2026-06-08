#include "Terrain.h"
#include "Mesh.h" // Vertex struct

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/noise.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
    // Fractal Brownian Motion: layered simplex noise for natural mountain shape.
    float fbm(glm::vec2 p, unsigned int seed) {
        glm::vec2 off(float(seed) * 17.3f, float(seed) * 9.1f);
        p += off;
        float value = 0.0f;
        float amp   = 0.5f;
        float freq  = 1.0f;
        for (int i = 0; i < 6; ++i) {
            value += amp * glm::simplex(p * freq);
            freq  *= 2.0f;
            amp   *= 0.5f;
        }
        return value; // roughly [-1, 1]
    }
}

Terrain::Terrain(int resolution, float worldSize, float maxHeight, unsigned int seed)
    : m_maxHeight(maxHeight) {

    const int   verts = resolution + 1;
    const float half  = worldSize * 0.5f;
    const float step  = worldSize / float(resolution);

    std::vector<Vertex> vertices(verts * verts);

    // --- heights ---
    auto heightAt = [&](float x, float z) -> float {
        // base coordinate in "noise space" (a few features across the terrain)
        glm::vec2 n = glm::vec2(x, z) / worldSize * 3.0f;
        float h = fbm(n, seed) * 0.5f + 0.5f;          // -> [0,1]
        h = std::pow(h, 1.6f);                          // sharpen peaks

        // radial island falloff: high in the middle, sinks to the sea at edges
        float r = std::sqrt(x * x + z * z) / half;      // 0 at centre, 1 at edge
        float island = std::clamp(1.0f - r, 0.0f, 1.0f);
        island = island * island;                       // smooth shoreline

        return (h * island * 2.0f - 0.35f) * maxHeight; // below 0 sinks underwater
    };

    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            float wx = x * step - half;
            float wz = z * step - half;
            float wy = heightAt(wx, wz);
            Vertex& v = vertices[z * verts + x];
            v.position = glm::vec3(wx, wy, wz);
            v.normal   = glm::vec3(0.0f, 1.0f, 0.0f); // filled below
            v.uv       = glm::vec2(float(x) / resolution, float(z) / resolution);
        }
    }

    // --- normals from finite differences of neighbouring heights ---
    for (int z = 0; z <= resolution; ++z) {
        for (int x = 0; x <= resolution; ++x) {
            int xl = std::max(x - 1, 0), xr = std::min(x + 1, resolution);
            int zd = std::max(z - 1, 0), zu = std::min(z + 1, resolution);
            float hl = vertices[z * verts + xl].position.y;
            float hr = vertices[z * verts + xr].position.y;
            float hd = vertices[zd * verts + x].position.y;
            float hu = vertices[zu * verts + x].position.y;
            glm::vec3 n = glm::normalize(glm::vec3(hl - hr, 2.0f * step, hd - hu));
            vertices[z * verts + x].normal = n;
        }
    }

    // --- indices ---
    std::vector<unsigned int> indices;
    indices.reserve(resolution * resolution * 6);
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            unsigned int tl = z * verts + x;
            unsigned int tr = tl + 1;
            unsigned int bl = (z + 1) * verts + x;
            unsigned int br = bl + 1;
            indices.insert(indices.end(), { tl, bl, tr,  tr, bl, br });
        }
    }
    m_indexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

Terrain::~Terrain() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

void Terrain::draw(const Shader& shader, const glm::vec3& worldPosition) const {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPosition);
    shader.setMat4("model", model);
    shader.setFloat("maxHeight", m_maxHeight);
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
