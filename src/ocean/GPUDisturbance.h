#ifndef GPU_DISTURBANCE_H
#define GPU_DISTURBANCE_H

#include "../graphics/Shader.h"
#include <glm/glm.hpp>

class GPUDisturbance {
public:
    // resolution: texture size (e.g. 256). worldSize: ocean diameter in metres.
    GPUDisturbance(unsigned int resolution, float worldSize);
    ~GPUDisturbance();

    void update(float dt);

    // worldPos: XZ world-space impact point. amplitude: crest height in metres.
    // sigmaTexels: Gaussian footprint radius of the injected pulse, in texels —
    // lets a small boat drop a tight ripple and a large ship a broad swell from
    // the same call site (texel = worldSize/resolution metres).
    void disturb(glm::vec2 worldPos, float amplitude = 3.0f, float sigmaTexels = 5.0f);

    unsigned int getHeightTexture() const { return m_tex[m_curr]; }

private:
    unsigned int m_resolution;
    float        m_worldSize;

    unsigned int m_tex[3]; // 0=curr, 1=prev, 2=next (rotated each frame)
    unsigned int m_curr, m_prev, m_next;

    Shader m_propagateShader;
    Shader m_injectShader;

    float m_waveC;
    float m_damping;

    void allocTexture(unsigned int& tex) const;
};

#endif
