#include "FoamMap.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>

FoamMap::FoamMap(int resolution, float oceanSize)
    : m_res(resolution), m_oceanSize(oceanSize), m_data(resolution * resolution, 0.0f) {
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_res, m_res, 0, GL_RED, GL_FLOAT, m_data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

FoamMap::~FoamMap() {
    if (m_tex) glDeleteTextures(1, &m_tex);
}

void FoamMap::splat(float worldX, float worldZ, float radius, float amount) {
    // world -> grid
    const float half = 0.5f * m_oceanSize;
    const float u = (worldX + half) / m_oceanSize; // [0,1]
    const float v = (worldZ + half) / m_oceanSize;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return;

    const float cx = u * m_res;
    const float cy = v * m_res;
    const float rPix = std::max(1.0f, radius / m_oceanSize * m_res);
    const int   r    = static_cast<int>(std::ceil(rPix));
    const int   ix0  = std::max(0, int(cx) - r), ix1 = std::min(m_res - 1, int(cx) + r);
    const int   iy0  = std::max(0, int(cy) - r), iy1 = std::min(m_res - 1, int(cy) + r);

    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            float dx = (x + 0.5f) - cx, dy = (y + 0.5f) - cy;
            float d = std::sqrt(dx * dx + dy * dy) / rPix;
            if (d > 1.0f) continue;
            float fall = 1.0f - d;            // soft disc
            float add  = amount * fall * fall;
            float& cell = m_data[y * m_res + x];
            cell = std::min(1.0f, cell + add);
        }
    }
    m_dirty = true;
}

void FoamMap::decay(float dt, float decayPerSec) {
    const float k = std::exp(-decayPerSec * dt);
    for (float& c : m_data) c *= k;
    m_dirty = true;
}

void FoamMap::upload() {
    if (!m_dirty) return;
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_res, m_res, GL_RED, GL_FLOAT, m_data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    m_dirty = false;
}
