#include "GPUDisturbance.h"

#include <vector>
#include <cmath>

namespace {
    constexpr float kWaveSpeed   = 12.0f;  // m/s — governs how fast disturbance rings spread
    constexpr float kDamping     = 0.998f; // energy retention per tick
    constexpr float kSigmaTexels = 5.0f;   // Gaussian radius of injected pulse (texels)
    constexpr float kFixedDt     = 1.0f / 60.0f;
}

GPUDisturbance::GPUDisturbance(unsigned int resolution, float worldSize)
    : m_resolution(resolution),
      m_worldSize(worldSize),
      m_curr(0), m_prev(1), m_next(2),
      m_propagateShader("../assets/shaders/disturbance_propagate.comp"),
      m_injectShader("../assets/shaders/disturbance_inject.comp"),
      m_damping(kDamping) {

    const float dx = worldSize / static_cast<float>(resolution);
    m_waveC = (kWaveSpeed * kWaveSpeed * kFixedDt * kFixedDt) / (dx * dx);

    for (int i = 0; i < 3; ++i)
        allocTexture(m_tex[i]);
}

GPUDisturbance::~GPUDisturbance() {
    glDeleteTextures(3, m_tex);
}

void GPUDisturbance::allocTexture(unsigned int& tex) const {
    const auto N = static_cast<GLsizei>(m_resolution);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, N, N);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const std::vector<float> zeros(m_resolution * m_resolution, 0.0f);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, N, N, GL_RED, GL_FLOAT, zeros.data());
}

void GPUDisturbance::update(float /*dt*/) {
    const unsigned int groups = (m_resolution + 15u) / 16u;

    m_propagateShader.use();
    m_propagateShader.setFloat("waveC",   m_waveC);
    m_propagateShader.setFloat("damping", m_damping);

    glBindImageTexture(0, m_tex[m_curr], 0, GL_FALSE, 0, GL_READ_ONLY,  GL_R32F);
    glBindImageTexture(1, m_tex[m_prev], 0, GL_FALSE, 0, GL_READ_ONLY,  GL_R32F);
    glBindImageTexture(2, m_tex[m_next], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

    glDispatchCompute(groups, groups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // Rotate buffers: next becomes current, current becomes prev, prev gets reused as next
    const unsigned int tmp = m_prev;
    m_prev = m_curr;
    m_curr = m_next;
    m_next = tmp;
}

void GPUDisturbance::disturb(glm::vec2 worldPos, float amplitude) {
    const glm::vec2 uv = worldPos / m_worldSize + glm::vec2(0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return;

    const unsigned int groups = (m_resolution + 15u) / 16u;

    m_injectShader.use();
    m_injectShader.setVec2("centerUV",    uv);
    m_injectShader.setFloat("amplitude",  amplitude);
    m_injectShader.setFloat("sigmaTexels", kSigmaTexels);

    // Read-write on current buffer so the pulse is visible immediately this frame
    glBindImageTexture(0, m_tex[m_curr], 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);

    glDispatchCompute(groups, groups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}
