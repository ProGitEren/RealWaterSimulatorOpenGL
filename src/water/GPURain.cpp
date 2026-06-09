#include "GPURain.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace {
    struct Drop { float pos[4]; float vel[4]; }; // matches the SSBO layout
    constexpr std::size_t kMaxStoredRipples = 4000;
    constexpr float kRippleTarget = 480.0f;   // ~ SSBO budget (512) in the water shader
    constexpr float kRippleRadius = 80.0f;    // rings spawn within this of the camera
    constexpr float kSpawnRadius  = 100.0f;
    float frand01() { return float(rand()) / float(RAND_MAX); }
}

GPURain::GPURain(unsigned int maxDrops)
    : m_maxDrops(maxDrops),
      m_updateShader("../assets/shaders/rain_update.comp") {

    // Drop SSBO, zero-initialised (pos.w == 0 -> the compute shader respawns it).
    // vector<Drop>(n) value-initialises each POD Drop to all-zero bytes already.
    std::vector<Drop> init(m_maxDrops);
    glGenBuffers(1, &m_ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, init.size() * sizeof(Drop), init.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glGenVertexArrays(1, &m_vao); // empty VAO for attributeless draws
}

GPURain::~GPURain() {
    glDeleteBuffers(1, &m_ssbo);
    glDeleteVertexArrays(1, &m_vao);
}

void GPURain::update(float dt, const glm::vec3& camPos, glm::vec2 windDrift,
                     float fallSpeed, int spawnRate, float rippleLifetime) {
    m_time += dt;

    // Active drop count scales with the spawn-rate slider. spawnRate is "drops per
    // frame" in the old system; here it maps to how many drops are alive (a denser
    // sheet). Cap to the buffer size.
    const float dropsPerSpawn = 24.0f; // each "spawn rate" unit keeps this many drops alive
    m_activeCount = std::min(m_maxDrops, static_cast<unsigned int>(spawnRate * dropsPerSpawn));

    // --- GPU: advance every drop in one compute dispatch ---
    if (m_activeCount > 0) {
        m_updateShader.use();
        m_updateShader.setFloat("uDt", dt);
        m_updateShader.setVec3 ("uCamPos", camPos);
        m_updateShader.setVec2 ("uWindDrift", windDrift);
        m_updateShader.setFloat("uFallSpeed", fallSpeed);
        m_updateShader.setFloat("uSpawnRadius", kSpawnRadius);
        m_updateShader.setUInt ("uCount", m_activeCount);
        m_updateShader.setFloat("uTime", m_time);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
        glDispatchCompute((m_activeCount + 255u) / 256u, 1, 1);
        // We read the drop SSBO in the vertex shader as storage (not as vertex
        // attributes), so only the shader-storage barrier is needed.
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // --- CPU: throttled ripple-ring spawner (cheap, feeds the water shader) ---
    // Independent of drop count: a steady ~480/lifetime rings around the camera so
    // the surface always shows rain rings without overflowing the 512-entry SSBO.
    const float rippleRate = kRippleTarget / std::max(rippleLifetime, 0.25f);
    m_rippleBudget = std::min(m_rippleBudget + rippleRate * dt, 20.0f);
    if (spawnRate > 0) {
        while (m_rippleBudget >= 1.0f) {
            m_rippleBudget -= 1.0f;
            float a = frand01() * 6.2831853f;
            float r = std::sqrt(frand01()) * kRippleRadius;
            glm::vec3 ring(camPos.x + std::cos(a) * r, camPos.z + std::sin(a) * r, 0.0f);
            if (m_ripples.size() >= kMaxStoredRipples) m_ripples.pop_front();
            m_ripples.push_back(ring);
        }
    }
    for (auto it = m_ripples.begin(); it != m_ripples.end(); ) {
        it->z += dt;
        if (it->z > rippleLifetime) it = m_ripples.erase(it);
        else ++it;
    }
}

void GPURain::render(Shader& streakShader, Shader& splashShader,
                     const glm::mat4& proj, const glm::mat4& view,
                     glm::vec2 windDrift, float dropSize,
                     float opacity, float splashHeight) {
    if (m_activeCount == 0) return;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
    glBindVertexArray(m_vao);
    glEnable(GL_LINE_SMOOTH);

    // --- Pass 1: rain streaks (thin, faint) ---
    streakShader.use();
    streakShader.setMat4 ("projection", proj);
    streakShader.setMat4 ("view", view);
    streakShader.setVec2 ("uWindDrift", windDrift);
    streakShader.setFloat("uDropSize", dropSize);
    streakShader.setFloat("uOpacity", opacity);
    glLineWidth(std::clamp(dropSize * 1.5f, 1.0f, 6.0f));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_activeCount) * 2);

    // --- Pass 2: splash columns (brighter, thicker) — same SSBO, no CPU work.
    // Inactive splashes emit a degenerate off-screen line, so this is cheap.
    splashShader.use();
    splashShader.setMat4 ("projection", proj);
    splashShader.setMat4 ("view", view);
    splashShader.setFloat("uSplashHeight", splashHeight);
    glLineWidth(2.5f);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_activeCount) * 2);

    glBindVertexArray(0);
    glDisable(GL_LINE_SMOOTH);
}
