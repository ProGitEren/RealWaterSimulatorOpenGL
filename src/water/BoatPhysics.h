#ifndef BOAT_PHYSICS_H
#define BOAT_PHYSICS_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>

// Spring-damper 6-DOF buoyancy for a floating hull on the FFT ocean.
//
// The hull is approximated by a grid of sample "facets" in the boat's local
// frame (length x beam). Each step:
//   - transform each facet to world space and look up the ocean surface height,
//   - average those heights and fit fore/aft + port/starboard height slopes,
//   - drive HEAVE with a critically-damped spring toward the average height,
//   - drive PITCH/ROLL with critically-damped springs toward the fitted slopes.
// A spring toward the surface is far more stable than summing raw Archimedes
// forces (which sink the hull unless perfectly tuned).
//
// Heave/pitch/roll emerge from the waves; the caller still drives yaw + forward
// motion (surge) for scripted navigation.
struct BoatPhysics {
    // --- state ---
    glm::vec3 position{0.0f};       // hull origin (waterline centre) world pos
    glm::quat orientation{1,0,0,0}; // current orientation (yaw * pitch * roll)
    float heaveVel = 0.0f;          // vertical velocity (m/s)
    float pitch = 0.0f, pitchVel = 0.0f; // rotation about local X (rad, rad/s)
    float roll  = 0.0f, rollVel  = 0.0f; // rotation about local Z (rad, rad/s)

    // --- tuning (sane defaults; exposed to ImGui) ---
    float length      = 90.0f;      // hull length (m), local +X (forward) span
    float beam        = 28.0f;      // hull width (m), local +Z span
    float buoyancy    = 3.0f;       // up-force per metre submerged (balances g near ~1m draft)
    float linearDamp  = 2.0f;       // heave damping (higher = settles faster, less bobbing)
    float angularDamp = 2.8f;       // pitch/roll damping (kills oscillation)
    int   facetsX     = 5;          // facets along length
    int   facetsZ     = 3;          // facets across beam
    float floatHeight = 0.0f;       // extra waterline offset (freeboard)

    bool  initialized = false;

    // heightAt(x,z) must return the ocean surface height (metres) at world XZ.
    void step(float dt, const glm::vec3& fixedXZ, float yaw,
              const std::function<float(float, float)>& heightAt);
};

#endif
