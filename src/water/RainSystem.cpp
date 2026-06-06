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

    // 1. SPAWN NEW DROPS
    for (int i = 0; i < 50; ++i) {
        float offsetX = (static_cast<float>(rand() % 2001) - 1000.0f) / 1000.0f * kSpawnRadius;
        float offsetZ = (static_cast<float>(rand() % 2001) - 1000.0f) / 1000.0f * kSpawnRadius;

        Raindrop drop;
        drop.worldX   = std::clamp(cameraPos.x + offsetX, -kOceanHalf, kOceanHalf);
        drop.worldZ   = std::clamp(cameraPos.z + offsetZ, -kOceanHalf, kOceanHalf);
        drop.worldY   = cameraPos.y + 30.0f + static_cast<float>(rand() % 25);
        drop.speed    = 65.0f + static_cast<float>(rand() % 25);
        drop.dropSize = 0.01f + static_cast<float>(rand() % 15) / 500.0f;
        drop.gridX    = 0;
        drop.gridZ    = 0;
        m_activeRain.push_back(drop);
    }

    // 2. UPDATE DROPS & TRIGGER IMPACTS
    m_rainVertices.clear();

    for (auto it = m_activeRain.begin(); it != m_activeRain.end(); ) {
        it->worldY -= it->speed * deltaTime;
        it->worldX += (it->speed * 0.2f) * deltaTime;
        it->worldZ += (it->speed * 0.1f) * deltaTime;

        if (it->worldY <= 0.5f) {
            float dx = it->worldX - cameraPos.x;
            float dz = it->worldZ - cameraPos.z;
            if (dx * dx + dz * dz < kRippleRadius * kRippleRadius) {
                if (m_activeRipples.size() >= kMaxStoredRipples)
                    m_activeRipples.pop_front();
                m_activeRipples.push_back(glm::vec3(it->worldX, it->worldZ, 0.0f));

                if (m_activeSplashes.size() < kMaxSplashes) {
                    Splash sp;
                    sp.worldX = it->worldX;
                    sp.worldZ = it->worldZ;
                    sp.age    = 0.0f;
                    sp.v0     = 2.5f + static_cast<float>(rand() % 20) / 10.0f; // 2.5-4.5 m/s
                    m_activeSplashes.push_back(sp);
                }
            }
            it = m_activeRain.erase(it);
        } else {
            // Rain streak: tip (falling end, alpha=1) → tail (alpha=0)
            const float streakLen = it->speed * 0.18f;
            m_rainVertices.push_back(it->worldX);
            m_rainVertices.push_back(it->worldY);
            m_rainVertices.push_back(it->worldZ);
            m_rainVertices.push_back(1.0f);

            m_rainVertices.push_back(it->worldX - it->speed * 0.2f * 0.12f);
            m_rainVertices.push_back(it->worldY + streakLen);
            m_rainVertices.push_back(it->worldZ - it->speed * 0.1f * 0.12f);
            m_rainVertices.push_back(0.0f);
            ++it;
        }
    }

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

    // 4. AGE RIPPLES
    for (auto it = m_activeRipples.begin(); it != m_activeRipples.end(); ) {
        it->z += deltaTime;
        if (it->z > 6.0f)
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
    glDrawArrays(GL_LINES, 0, m_rainVertices.size() / 4);
    glBindVertexArray(0);

    glDisable(GL_LINE_SMOOTH);
}
