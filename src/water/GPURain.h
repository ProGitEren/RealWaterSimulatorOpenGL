#ifndef GPU_RAIN_H
#define GPU_RAIN_H

#include "../graphics/Shader.h"
#include <glm/glm.hpp>
#include <deque>
#include <vector>

// Fully GPU-driven rain. All drops live in an SSBO and are advanced by a compute
// shader (rain_update.comp) — no CPU per-drop loop, no per-frame vertex upload.
// They are drawn attributeless/instanced (rain_gpu.vert reads the SSBO directly).
//
// Ripple rings are also GPU-driven: when a drop hits the water, the compute shader
// writes the impact straight into the water shader's ripple ring buffer (binding 4,
// atomic head at binding 5). So each ring is born at the EXACT hit point and frame,
// in lockstep with the splash — there is NO CPU ripple spawner.
class GPURain {
public:
    GPURain(unsigned int maxDrops);
    ~GPURain();

    // Advance all drops on the GPU; impacts write ripples into rippleSSBO (binding
    // 4) via the atomic head in rippleHeadSSBO (binding 5), capped to rippleCap.
    void update(float dt, const glm::vec3& camPos, glm::vec2 windDrift,
                float fallSpeed, int spawnRate,
                unsigned int rippleSSBO, unsigned int rippleHeadSSBO,
                unsigned int rippleCap);

    // Attributeless instanced draw of all active drop streaks + splash columns
    // (two draw calls, both reading the same SSBO — no CPU vertex work).
    void render(Shader& streakShader, Shader& splashShader,
                const glm::mat4& proj, const glm::mat4& view,
                glm::vec2 windDrift, float dropSize,
                float opacity, float splashHeight);

private:
    unsigned int m_maxDrops;
    unsigned int m_activeCount = 0;
    unsigned int m_ssbo = 0;     // drop buffer (binding 0)
    unsigned int m_vao  = 0;     // empty VAO for attributeless draw

    Shader m_updateShader;       // rain_update.comp
    float m_time = 0.0f;
};

#endif
