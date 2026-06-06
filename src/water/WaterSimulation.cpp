#include "WaterSimulation.h"
#include <algorithm>
#include <cmath>

WaterSimulation::WaterSimulation(int gridSize, float spatialStep, float timeStep, float waveSpeed, float damping)
    : m_gridSize(gridSize), m_spatialStep(spatialStep), m_timeStep(timeStep), m_waveSpeed(waveSpeed), m_damping(damping) {
    
    m_numVertices = m_gridSize + 1; // A 50x50 grid of squares has 51x51 actual points
    int totalVertices = m_numVertices * m_numVertices;

    m_currentHeights.resize(totalVertices, 0.0f);
    m_previousHeights.resize(totalVertices, 0.0f);
    m_newHeights.resize(totalVertices, 0.0f);
}

int WaterSimulation::getIndex(int x, int z) const {
    return z * m_numVertices + x;
}

void WaterSimulation::disturb(int centerX, int centerZ, float amount) {
    int radius = 3;
    float sigma = 2.0f; // bigger = softer splash

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int x = centerX + dx;
            int z = centerZ + dz;

            if (x > 0 && x < m_gridSize && z > 0 && z < m_gridSize) {
                float dist2 = static_cast<float>(dx * dx + dz * dz);
                float weight = std::exp(-dist2 / (2.0f * sigma * sigma));
                m_currentHeights[getIndex(x, z)] += amount * weight;
            }
        }
    }
}

void WaterSimulation::disturbRain(int centerX, int centerZ, float amount) {
    int radius = 2;         // How wide the raindrop splash is (3 cells in every direction)
    float spread = 1.5f;    // How "soft" or "sharp" the bell curve is

    // Loop through a 7x7 square around the target center
    for (int z = -radius; z <= radius; ++z) {
        for (int x = -radius; x <= radius; ++x) {
            
            int targetX = centerX + x;
            int targetZ = centerZ + z;

            // SAFETY CHECK: Make sure we don't accidentally push the locked 
            // boundary walls or go outside the array limits!
            if (targetX > 0 && targetX < (m_gridSize - 1) && 
                targetZ > 0 && targetZ < (m_gridSize - 1)) {
                
                // 1. Calculate the distance squared from the center of the splash
                float distanceSquared = (float)(x * x + z * z);
                
                // 2. Calculate the Gaussian bell curve push
                // std::exp is the C++ function for 'e' to the power of...
                float pushAmount = amount * std::exp(-distanceSquared / spread);
                
                // 3. Apply the smooth push to the physics grid array!
                m_currentHeights[getIndex(targetX, targetZ)] += pushAmount; 
            }
        }
    }
}

float WaterSimulation::getHeight(int x, int z) const {
    return m_currentHeights[getIndex(x, z)];
}

void WaterSimulation::update() {
    // Constant for the wave equation: (c^2 * dt^2) / dx^2
    float c = (m_waveSpeed * m_waveSpeed * m_timeStep * m_timeStep) / (m_spatialStep * m_spatialStep);

    // Loop through all internal vertices (skip the absolute edges of the grid)
    for (int z = 1; z < m_gridSize; ++z) {
        for (int x = 1; x < m_gridSize; ++x) {
            int i = getIndex(x, z);

            // 1. Calculate the Laplacian (sum of 4 neighbors - 4*current)
            float laplacian = 
                m_currentHeights[getIndex(x - 1, z)] + // Left
                m_currentHeights[getIndex(x + 1, z)] + // Right
                m_currentHeights[getIndex(x, z - 1)] + // Down
                m_currentHeights[getIndex(x, z + 1)] - // Up
                (4.0f * m_currentHeights[i]);

            // 2. The Discrete Wave Equation with Damping
            m_newHeights[i] = (2.0f * m_currentHeights[i] - m_previousHeights[i] + c * laplacian) * m_damping;
        }
    }

    // 3. Swap the buffers for the next frame
    m_previousHeights = m_currentHeights;
    m_currentHeights = m_newHeights;
}
