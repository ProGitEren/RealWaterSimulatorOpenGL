#ifndef TERRAIN_H
#define TERRAIN_H

#include "Shader.h"
#include <glm/glm.hpp>

// A noise-displaced grid that forms mountains / an island rising out of the
// ocean. Generated procedurally with fractal noise + a radial island falloff
// so the edges sink below the waterline. Owns its GL objects (RAII).
class Terrain {
public:
    // worldSize: width/depth in metres. resolution: grid vertices per side.
    // maxHeight: peak elevation in metres. seed: varies the mountain shape.
    Terrain(int resolution, float worldSize, float maxHeight, unsigned int seed);
    ~Terrain();

    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // Draws with the given shader; the model matrix (position) is applied here.
    void draw(const Shader& shader, const glm::vec3& worldPosition) const;

    float getMaxHeight() const { return m_maxHeight; }

private:
    unsigned int m_vao = 0, m_vbo = 0, m_ebo = 0;
    int   m_indexCount = 0;
    float m_maxHeight  = 0.0f;
};

#endif
