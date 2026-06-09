#include "RainSystem.h"
#include "WaterSimulation.h"
#include <glad/glad.h>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cmath>

RainSystem::RainSystem(int gridSize, float tileSize)
    : m_gridSize(gridSize), m_tileSize(tileSize) {

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    // Stride = 4 floats: xyz position + alpha
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

RainSystem::~RainSystem() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

void RainSystem::update(float deltaTime, const glm::vec3& cameraPos, WaterSimulation* waterSimulation) {
    constexpr std::size_t kMaxStoredRipples = 4000;
    constexpr std::size_t kMaxSplashes      = 600;
    constexpr float kSpawnRadius = 100.0f;
    constexpr float kOceanHalf   = 512.0f;
    constexpr float kRippleRadius = 80.0f;
    constexpr float kGravity      = 9.8f;

    // Throttle ripple births so the active count fits the 512-entry render SSBO.
    // Without this, every impact spawns a ripple (~1800/s), the buffer keeps only
    // the newest 512, and old ones get evicted ~0.3 s in — so raising the lifetime
    // did nothing. The rate adapts to lifetime (faster when short-lived, slower
    // when they linger) so the field stays ≈ full buffer at any setting and the
    // lifetime slider purely controls how long each ripple survives.
    constexpr float kRippleTarget = 480.0f;               // ~ SSBO budget (512)
    const float rippleRate = kRippleTarget / std::max(rippleLifetime, 0.25f);
    m_rippleBudget = std::min(m_rippleBudget + rippleRate * deltaTime, 20.0f);

    // 1. SPAWN NEW DROPS
    for (int i = 0; i < spawnRate; ++i) {
        float offsetX = (static_cast<float>(rand() % 2001) - 1000.0f) / 1000.0f * kSpawnRadius;
        float offsetZ = (static_cast<float>(rand() % 2001) - 1000.0f) / 1000.0f * kSpawnRadius;

        Raindrop drop;
        drop.worldX   = std::clamp(cameraPos.x + offsetX, -kOceanHalf, kOceanHalf);
        drop.worldZ   = std::clamp(cameraPos.z + offsetZ, -kOceanHalf, kOceanHalf);
        drop.worldY   = cameraPos.y + 30.0f + static_cast<float>(rand() % 25);
        drop.speed    = fallSpeed + static_cast<float>(rand() % 20) - 10.0f;
        drop.dropSize = 0.01f + static_cast<float>(rand() % 15) / 500.0f;
        drop.gridX    = 0;
        drop.gridZ    = 0;
        m_activeRain.push_back(drop);
    }

    // 2. UPDATE DROPS & TRIGGER IMPACTS
    m_rainVertices.clear();

    for (auto it = m_activeRain.begin(); it != m_activeRain.end(); ) {
        it->worldY -= it->speed * deltaTime;
        it->worldX += windDrift.x * deltaTime;
        it->worldZ += windDrift.y * deltaTime;

        if (it->worldY <= 0.5f) {
            float dx = it->worldX - cameraPos.x;
            float dz = it->worldZ - cameraPos.z;
            if (dx * dx + dz * dz < kRippleRadius * kRippleRadius) {
                // Throttle ripple births to the steady budget so the active count
                // never overflows the 512-entry SSBO (which evicts oldest first).
                if (m_rippleBudget >= 1.0f) {
                    m_rippleBudget -= 1.0f;
                    if (m_activeRipples.size() >= kMaxStoredRipples)
                        m_activeRipples.pop_front();
                    m_activeRipples.push_back(glm::vec3(it->worldX, it->worldZ, 0.0f));
                }

                if (m_activeSplashes.size() < kMaxSplashes) {
                    Splash sp;
                    sp.worldX = it->worldX;
                    sp.worldZ = it->worldZ;
                    sp.age    = 0.0f;
                    sp.v0     = (2.5f + static_cast<float>(rand() % 20) / 10.0f) * splashHeight;
                    m_activeSplashes.push_back(sp);
                }
            }
            it = m_activeRain.erase(it);
        } else {
            // Rain streak: tip (falling end, alpha=1) -> tail (alpha=0), slanted
            // along the drop velocity (down + wind drift) so it matches the wind.
            const float streakLen = it->speed * 0.18f * dropSize;
            const glm::vec3 dir = glm::normalize(glm::vec3(windDrift.x, -it->speed, windDrift.y));
            m_rainVertices.push_back(it->worldX);
            m_rainVertices.push_back(it->worldY);
            m_rainVertices.push_back(it->worldZ);
            m_rainVertices.push_back(1.0f);

            m_rainVertices.push_back(it->worldX - dir.x * streakLen);
            m_rainVertices.push_back(it->worldY - dir.y * streakLen);
            m_rainVertices.push_back(it->worldZ - dir.z * streakLen);
            m_rainVertices.push_back(0.0f);
            ++it;
        }
    }

    // Splashes are appended after the rain streaks; remember where they start so
    // render() can draw them as a separate, brighter, thicker "water column" pass.
    m_splashStart = m_rainVertices.size();

    // 3. UPDATE SPLASHES — parabolic rise, fade out, append to same vertex buffer
    for (auto it = m_activeSplashes.begin(); it != m_activeSplashes.end(); ) {
        it->age += deltaTime;
        const float lifetime = 2.0f * it->v0 / kGravity; // time to rise and fall back
        if (it->age >= lifetime) {
            it = m_activeSplashes.erase(it);
            continue;
        }
        float h     = it->v0 * it->age - 0.5f * kGravity * it->age * it->age;
        float alpha = 1.0f - it->age / lifetime; // fades as it rises and falls

        // Base at water surface (transparent)
        m_rainVertices.push_back(it->worldX);
        m_rainVertices.push_back(0.5f);
        m_rainVertices.push_back(it->worldZ);
        m_rainVertices.push_back(0.0f);

        // Rising tip (bright, fades over lifetime)
        m_rainVertices.push_back(it->worldX);
        m_rainVertices.push_back(0.5f + h);
        m_rainVertices.push_back(it->worldZ);
        m_rainVertices.push_back(alpha);

        ++it;
    }

    // 4. AGE RIPPLES — live the full shader lifetime so they fade out naturally
    for (auto it = m_activeRipples.begin(); it != m_activeRipples.end(); ) {
        it->z += deltaTime;
        if (it->z > rippleLifetime)
            it = m_activeRipples.erase(it);
        else
            ++it;
    }
}

void RainSystem::render(Shader& rainShader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& viewPos) {
    if (m_rainVertices.empty()) return;

    rainShader.use();
    rainShader.setMat4("projection", projection);
    rainShader.setMat4("view", view);
    rainShader.setMat4("model", glm::mat4(1.0f));
    rainShader.setVec3("viewPos", viewPos);

    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_rainVertices.size() * sizeof(float), m_rainVertices.data(), GL_DYNAMIC_DRAW);

    const int streakVerts = static_cast<int>(m_splashStart / 4);
    const int totalVerts  = static_cast<int>(m_rainVertices.size() / 4);

    // Rain streaks — thin, faint, tinted by the opacity slider.
    rainShader.setInt("uIsSplash", 0);
    glLineWidth(std::clamp(dropSize * 1.5f, 1.0f, 6.0f));
    glDrawArrays(GL_LINES, 0, streakVerts);

    // Splash jets — brighter + thicker so the splash-height slider reads as a
    // visible water column rising off the surface, distinct from the rain.
    if (totalVerts > streakVerts) {
        rainShader.setInt("uIsSplash", 1);
        glLineWidth(2.5f);
        glDrawArrays(GL_LINES, streakVerts, totalVerts - streakVerts);
    }

    glBindVertexArray(0);

    glDisable(GL_LINE_SMOOTH);
}
