#ifndef FOAM_MAP_H
#define FOAM_MAP_H

#include <vector>
#include <glm/glm.hpp>

// A world-space foam coverage texture for the ocean surface. Moving vehicles
// "paint" foam into it along their path (bow + hull + stern trail); it fades
// over time. The water shader samples this map and blends white foam where
// coverage is high — so foam is part of the SURFACE (no sprite overdraw on the
// hull, unlike a particle system).
//
// The map covers the ocean patch [-oceanSize/2, +oceanSize/2] in X and Z.
class FoamMap {
public:
    FoamMap(int resolution, float oceanSize);
    ~FoamMap();

    // Paint a soft foam disc at world (x,z). amount in [0,1], radius in metres.
    void splat(float worldX, float worldZ, float radius, float amount);

    // Fade everything toward 0 (foam dissipates). decayPerSec ~ 0.4..1.0.
    void decay(float dt, float decayPerSec);

    // Upload the CPU grid to the GL texture (call once per frame after edits).
    void upload();

    unsigned int texture() const { return m_tex; }
    float oceanSize() const { return m_oceanSize; }

private:
    int   m_res;
    float m_oceanSize;
    bool  m_dirty = false;
    std::vector<float> m_data;   // coverage [0,1], row-major
    unsigned int m_tex = 0;
};

#endif
