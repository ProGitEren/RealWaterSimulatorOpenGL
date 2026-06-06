#pragma once

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

private:
    int m_gridSize;
    float m_tileSize;

    std::vector<Raindrop> m_activeRain;
    std::vector<Splash>   m_activeSplashes;
    std::deque<glm::vec3> m_activeRipples;
    std::vector<float>    m_rainVertices;

    unsigned int m_VAO, m_VBO;
};
