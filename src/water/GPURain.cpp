#include "GPURain.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace {
    struct Drop { float pos[4]; float vel[4]; }; // matches the SSBO layout
    constexpr float kSpawnRadius = 100.0f;        // drops live in this disc around the camera
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
                     float fallSpeed, int spawnRate,
                     unsigned int rippleSSBO, unsigned int rippleHeadSSBO,
                     unsigned int rippleCap) {
    m_time += dt;

    // Active drop count scales with the spawn-rate slider — it maps to how many
    // drops are alive (a denser sheet). Cap to the buffer size.
    const float dropsPerSpawn = 24.0f; // each "spawn rate" unit keeps this many drops alive
    m_activeCount = std::min(m_maxDrops, static_cast<unsigned int>(spawnRate * dropsPerSpawn));
    if (m_activeCount == 0) return;

    // One compute dispatch advances every drop AND writes a ripple ring at each
    // impact straight into the water shader's ripple buffer (binding 4), so rings
    // are perfectly synced with the splashes — no CPU spawner.
    m_updateShader.use();
    m_updateShader.setFloat("uDt", dt);
    m_updateShader.setVec3 ("uCamPos", camPos);
    m_updateShader.setVec2 ("uWindDrift", windDrift);
    m_updateShader.setFloat("uFallSpeed", fallSpeed);
    m_updateShader.setFloat("uSpawnRadius", kSpawnRadius);
    m_updateShader.setUInt ("uCount", m_activeCount);
    m_updateShader.setFloat("uTime", m_time);
    m_updateShader.setUInt ("uRippleCap", rippleCap);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, rippleSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, rippleHeadSSBO);
    glDispatchCompute((m_activeCount + 255u) / 256u, 1, 1);
    // Drops are read as storage in the vertex shader; ripples are read by the
    // water fragment shader — both are SSBO reads, so the storage barrier covers it.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void GPURain::render(Shader& streakShader, Shader& splashShader,
                     const glm::mat4& proj, const glm::mat4& view,
                     glm::vec2 windDrift, float dropSize,
                     float opacity, float splashHeight) {
    if (m_activeCount == 0) return;

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssbo);
    glBindVertexArray(m_vao);
    // NOTE: GL_LINE_SMOOTH is intentionally NOT enabled — antialiased lines fall
    // back to a slow/buggy path on many Windows drivers (and can balloon driver
    // memory). Plain aliased lines look fine for rain and are portable + cheap.

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
}
