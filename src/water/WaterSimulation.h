#ifndef WATERSIMULATION_H
#define WATERSIMULATION_H

#include <vector>

class WaterSimulation {
public:
    // gridSize matches the 50x50 cells from our main.cpp
    WaterSimulation(int gridSize, float spatialStep, float timeStep, float waveSpeed, float damping);

    // Runs the wave equation math for one frame
    void update();

    // Pushes a vertex up or down to start a wave
    void disturb(int x, int z, float amount);

    // Rain drop disturbance
    void disturbRain(int centerX, int centerZ, float amount);

    // Get the calculated heights to send to OpenGL
    float getHeight(int x, int z) const;

private:
    int m_gridSize;
    int m_numVertices; // gridSize + 1
    float m_spatialStep;
    float m_timeStep;
    float m_waveSpeed;
    float m_damping;

    // The 3 arrays needed for the Verlet integration wave math
    std::vector<float> m_currentHeights;
    std::vector<float> m_previousHeights;
    std::vector<float> m_newHeights;

    // Helper to get 1D array index from 2D coordinates
    int getIndex(int x, int z) const;
};

#endif
