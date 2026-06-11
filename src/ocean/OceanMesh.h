#ifndef OCEAN_MESH_H
#define OCEAN_MESH_H

#include "../graphics/Shader.h"
#include <glm/glm.hpp>

class OceanMesh {
public:
    OceanMesh(int resolution, float tileSize);
    ~OceanMesh();

    void draw(const Shader& shader) const;

    float getWorldSize() const { return static_cast<float>(m_resolution) * m_tileSize; }

private:
    unsigned int m_vao;
    unsigned int m_vbo;
    unsigned int m_ebo;
    GLsizei      m_indexCount;
    int          m_resolution;
    float        m_tileSize;
};

#endif
