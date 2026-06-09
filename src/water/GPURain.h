#ifndef GPU_RAIN_H
#define GPU_RAIN_H

#include "../graphics/Shader.h"
#include <glm/glm.hpp>
#include <deque>
#include <vector>

// Fully GPU-driven rain. All drops live in an SSBO and are advanced by a compute
// shader (rain_update.comp) — no CPU per-drop loop, no per-frame vertex upload.
// They are drawn attributeless/instanced (rain_gpu.vert reads the SSBO directly),
// one draw call.
//
// The only CPU-side piece is the ripple-ring spawner: rain impacts must feed the
// water shader's ripple SSBO, and that field is intentionally throttled to ~480
// rings (independent of drop count), so it is cheap and stays on the CPU.
class GPURain {
public:
    GPURain(unsigned int maxDrops);
    ~GPURain();

    // Advance all drops on the GPU. Also spawns throttled ripple rings on the CPU
    // around the camera so the water surface still shows rain rings.
    void update(float dt, const glm::vec3& camPos, glm::vec2 windDrift,
                float fallSpeed, int spawnRate, float rippleLifetime);

    // Attributeless instanced draw of all active drop streaks + splash columns
    // (two draw calls, both reading the same SSBO — no CPU vertex work).
    void render(Shader& streakShader, Shader& splashShader,
                const glm::mat4& proj, const glm::mat4& view,
                glm::vec2 windDrift, float fallSpeed, float dropSize,
                float opacity, float splashHeight);

    const std::deque<glm::vec3>& getActiveRipples() const { return m_ripples; }

private:
    unsigned int m_maxDrops;
    unsigned int m_activeCount = 0;
    unsigned int m_ssbo = 0;     // drop buffer (binding 0)
    unsigned int m_vao  = 0;     // empty VAO for attributeless draw

    Shader m_updateShader;       // rain_update.comp

    // CPU ripple-ring field (throttled; feeds the water shader)
    std::deque<glm::vec3> m_ripples;
    float m_rippleBudget = 0.0f;
    float m_time = 0.0f;
};

#endif
