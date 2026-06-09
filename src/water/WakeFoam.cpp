#include "WakeFoam.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace {
    constexpr size_t kMaxParticles = 16000;
    float frand01() { return float(rand()) / float(RAND_MAX); }
    float frand(float a, float b) { return a + (b - a) * frand01(); }
}

WakeFoam::WakeFoam() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // per-particle: vec3 pos, float size, float alpha, float seed  (6 floats)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
}

WakeFoam::~WakeFoam() {
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

void WakeFoam::emit(const glm::vec3& pos, const glm::vec3& forward,
                    float speed, float halfWidth, float length, float intensity) {
    if (speed < 0.4f) return;                       // no wake when nearly stopped
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    const float halfLen   = 0.5f * length;
    const float speedF    = glm::clamp(speed / 12.0f, 0.0f, 1.6f);

    auto add = [&](glm::vec3 p, glm::vec3 vel, float life, float size) {
        if (m_particles.size() >= kMaxParticles) return;
        Particle q;
        q.pos = p; q.vel = vel; q.age = 0.0f;
        q.life = life; q.size = size; q.seed = frand01();
        m_particles.push_back(q);
    };

    // Emission counts scale with hull size + speed + intensity, accumulated so
    // it's framerate-independent. We lay down a CONTINUOUS band, not sparse puffs.
    const float scale = (0.6f + speedF) * intensity;
    m_emitAccum += scale * (1.0f / 60.0f);
    const float budget = m_emitAccum;               // fractional carries over below

    // --- 1) BOW WASH: a bright curved band hugging the front of the hull ---
    {
        int n = static_cast<int>(budget * (10.0f + halfWidth * 1.2f));
        for (int i = 0; i < n; ++i) {
            float s = frand(-1.0f, 1.0f);            // across the bow
            float fwdAmt = halfLen * (0.78f + 0.18f * (1.0f - std::abs(s)));
            glm::vec3 p = pos + forward * fwdAmt + right * (s * halfWidth * 0.9f);
            p.y += 0.18f;
            glm::vec3 vel = (right * s * frand(0.3f, 0.9f) + forward * frand(-0.1f, 0.3f)) * (1.0f + speedF);
            vel.y = frand(0.1f, 0.3f);
            add(p, vel, frand(0.5f, 1.1f), frand(1.0f, 2.2f) + halfWidth * 0.04f);
        }
    }

    // --- 2) HULL-SIDE LINES: foam running down both flanks of the boat ---
    {
        int n = static_cast<int>(budget * (8.0f + length * 0.12f));
        for (int i = 0; i < n; ++i) {
            float side = (frand01() < 0.5f ? -1.0f : 1.0f);
            float along = frand(-halfLen * 0.9f, halfLen * 0.85f);   // bow..stern
            glm::vec3 p = pos + forward * along + right * side * (halfWidth * frand(0.85f, 1.05f));
            p.y += 0.14f;
            glm::vec3 vel = (right * side * frand(0.25f, 0.7f) - forward * frand(0.0f, 0.4f)) * (0.8f + speedF);
            vel.y = frand(0.04f, 0.16f);
            add(p, vel, frand(0.7f, 1.6f), frand(0.8f, 1.8f));
        }
    }

    // --- 3) STERN CHURN + spreading V wake: the widest, longest-lived foam ---
    {
        int n = static_cast<int>(budget * (14.0f + speedF * 10.0f));
        for (int i = 0; i < n; ++i) {
            float side = (frand01() < 0.5f ? -1.0f : 1.0f);
            float behind = frand(0.0f, halfLen * 0.6f);
            float spread = frand(0.0f, halfWidth * 1.1f);
            glm::vec3 p = pos - forward * (halfLen * 0.85f + behind) + right * side * spread;
            p.y += 0.16f;
            // churn outward + backward -> the classic widening V
            glm::vec3 vel = (-forward * frand(0.4f, 1.0f) + right * side * frand(0.5f, 1.3f)) * (0.7f + speedF * 1.1f);
            vel.y = frand(0.06f, 0.28f);
            add(p, vel, frand(1.3f, 2.8f), frand(1.4f, 3.2f) + speedF * 1.0f);
        }
    }

    m_emitAccum = budget - std::floor(budget);
}

void WakeFoam::update(float dt) {
    for (auto& p : m_particles) {
        p.age += dt;
        p.pos += p.vel * dt;
        p.vel *= (1.0f - 1.5f * dt);   // drag — foam slows as it spreads
        p.vel.y -= 0.4f * dt;          // settle back toward the surface
        p.size += dt * 0.8f;           // foam patch grows as it dissipates
    }
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const Particle& p) { return p.age >= p.life; }),
        m_particles.end());
}

void WakeFoam::render(Shader& shader, const glm::mat4& proj, const glm::mat4& view, const glm::vec3& viewPos) {
    if (m_particles.empty()) return;

    m_instance.clear();
    m_instance.reserve(m_particles.size() * 6);
    for (const auto& p : m_particles) {
        float t = p.age / p.life;
        // Fade in fast, fade out slow (foam churns then dissolves).
        float alpha = (t < 0.15f) ? (t / 0.15f) : (1.0f - (t - 0.15f) / 0.85f);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        m_instance.insert(m_instance.end(),
            { p.pos.x, p.pos.y, p.pos.z, p.size, alpha, p.seed });
    }

    shader.use();
    shader.setMat4("projection", proj);
    shader.setMat4("view", view);
    shader.setVec3("viewPos", viewPos);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(GL_FALSE);   // foam is soft/transparent — don't write depth

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_instance.size() * sizeof(float), m_instance.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_particles.size()));
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_PROGRAM_POINT_SIZE);
}
