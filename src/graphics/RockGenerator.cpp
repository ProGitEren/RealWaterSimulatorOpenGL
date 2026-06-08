#include "RockGenerator.h"

#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>   // glm::simplex
#include <map>
#include <cmath>

namespace {
    // --- icosahedron base ---
    void buildIcosahedron(std::vector<glm::vec3>& verts, std::vector<unsigned int>& tris) {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        verts = {
            {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
            { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
            { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
        };
        for (auto& v : verts) v = glm::normalize(v);
        tris = {
            0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
            1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
            3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
            4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
        };
    }

    // Midpoint cache so shared edges produce shared vertices.
    unsigned int midpoint(unsigned int a, unsigned int b,
                          std::vector<glm::vec3>& verts,
                          std::map<uint64_t, unsigned int>& cache) {
        uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        glm::vec3 m = glm::normalize((verts[a] + verts[b]) * 0.5f);
        unsigned int idx = static_cast<unsigned int>(verts.size());
        verts.push_back(m);
        cache[key] = idx;
        return idx;
    }
}

void generateRock(unsigned int subdivisions, unsigned int seed,
                  std::vector<Vertex>& outVertices,
                  std::vector<unsigned int>& outIndices) {
    std::vector<glm::vec3> verts;
    std::vector<unsigned int> tris;
    buildIcosahedron(verts, tris);

    // Subdivide
    for (unsigned int s = 0; s < subdivisions; ++s) {
        std::map<uint64_t, unsigned int> cache;
        std::vector<unsigned int> next;
        next.reserve(tris.size() * 4);
        for (size_t i = 0; i < tris.size(); i += 3) {
            unsigned int a = tris[i], b = tris[i + 1], c = tris[i + 2];
            unsigned int ab = midpoint(a, b, verts, cache);
            unsigned int bc = midpoint(b, c, verts, cache);
            unsigned int ca = midpoint(c, a, verts, cache);
            next.insert(next.end(), { a, ab, ca,  b, bc, ab,  c, ca, bc,  ab, bc, ca });
        }
        tris.swap(next);
    }

    // Displace each unit-sphere vertex by layered simplex noise to make it rocky.
    glm::vec3 seedOffset(float(seed) * 13.13f, float(seed) * 7.77f, float(seed) * 3.33f);
    for (auto& v : verts) {
        glm::vec3 p = v * 2.3f + seedOffset;
        float n = 0.0f;
        n += 0.50f * glm::simplex(p);
        n += 0.25f * glm::simplex(p * 2.1f);
        n += 0.13f * glm::simplex(p * 4.3f);
        // Squash vertically a touch so it sits like a boulder, not a ball.
        float radius = 1.0f + 0.45f * n;
        v *= radius;
        v.y *= 0.8f;
    }

    // Build vertices with placeholder normals, accumulate face normals.
    outVertices.resize(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        outVertices[i].position = verts[i];
        outVertices[i].normal   = glm::vec3(0.0f);
        // simple planar UV from position (good enough for a noise/material look)
        outVertices[i].uv = glm::vec2(verts[i].x * 0.5f + 0.5f, verts[i].z * 0.5f + 0.5f);
    }
    outIndices = tris;

    for (size_t i = 0; i < tris.size(); i += 3) {
        unsigned int ia = tris[i], ib = tris[i + 1], ic = tris[i + 2];
        glm::vec3 fn = glm::cross(verts[ib] - verts[ia], verts[ic] - verts[ia]);
        outVertices[ia].normal += fn;
        outVertices[ib].normal += fn;
        outVertices[ic].normal += fn;
    }
    for (auto& vert : outVertices) {
        vert.normal = glm::normalize(vert.normal);
    }
}
