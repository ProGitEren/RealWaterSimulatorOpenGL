#ifndef WAKE_FOAM_H
#define WAKE_FOAM_H

#include <vector>
#include <glm/glm.hpp>
#include "../graphics/Shader.h"

// A lightweight foam-particle system for vehicle wakes: soft white blobs spawned
// behind and to the sides of a moving boat that expand, drift, and fade out on
// the water surface. Rendered as camera-facing round point sprites.
class WakeFoam {
public:
    WakeFoam();
    ~WakeFoam();

    // Emit foam for one moving vehicle this frame. `pos` is the hull centre on
    // the water, `forward` the unit travel direction, `speed` m/s, `halfWidth`
    // the hull half-width, `length` the bow-to-stern length. Produces a
    // continuous foam band: bow wash, hull-side lines, and a churning stern wake.
    void emit(const glm::vec3& pos, const glm::vec3& forward,
              float speed, float halfWidth, float length, float intensity);

    void update(float dt);
    void render(Shader& shader, const glm::mat4& proj, const glm::mat4& view, const glm::vec3& viewPos);

private:
    struct Particle {
        glm::vec3 pos;
        glm::vec3 vel;
        float age;
        float life;
        float size;
        float seed;
    };
    std::vector<Particle> m_particles;
    std::vector<float>     m_instance;   // packed: x,y,z, size, alpha, seed
    unsigned int m_vao = 0, m_vbo = 0;
    float m_emitAccum = 0.0f;
};

#endif
