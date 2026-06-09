#pragma once

#include <cstddef>
#include <deque>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../graphics/Shader.h" // Adjust path if your Shader class is somewhere else

class WaterSimulation;

struct Raindrop {
    int gridX, gridZ;
    float worldX, worldY, worldZ;
    float speed;
    float dropSize;
};

struct Splash {
    float worldX, worldZ;
    float age;
    float v0; // initial upward speed (m/s)
};

class RainSystem {
public:
    RainSystem(int gridSize, float tileSize);
    ~RainSystem();

    // Handles spawning, gravity, and triggering splashes
    void update(float deltaTime, const glm::vec3& cameraPos, WaterSimulation* waterSimulation = nullptr);
    
    // Handles sending the lines to the GPU
    void render(Shader& rainShader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& viewPos);

    const std::deque<glm::vec3>& getActiveRipples() const { return m_activeRipples; }

    // --- Runtime-tunable knobs (set from the UI each frame; sane defaults) ---
    int       spawnRate      = 50;     // drops per frame (rain intensity)
    float     fallSpeed      = 77.0f;  // base fall speed (m/s)
    float     dropSize       = 1.0f;   // streak length + line-width multiplier
    float     splashHeight   = 1.0f;   // splash jet height multiplier
    float     rippleLifetime = 6.0f;   // seconds a ripple ring lives
    glm::vec2 windDrift      = glm::vec2(10.0f, 7.0f); // horizontal wind velocity (m/s)

private:
    int m_gridSize;
    float m_tileSize;

    std::vector<Raindrop> m_activeRain;
    std::vector<Splash>   m_activeSplashes;
    std::deque<glm::vec3> m_activeRipples;
    std::vector<float>    m_rainVertices;
    std::size_t           m_splashStart = 0; // float index where splash verts begin
    float                 m_rippleBudget = 0.0f; // steady ripple-creation accumulator

    unsigned int m_VAO, m_VBO;
};
