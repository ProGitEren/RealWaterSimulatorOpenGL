#include "BoatPhysics.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace {
    constexpr float kMaxStepSeconds      = 1.0f / 30.0f; // clamp big steps for stability
    constexpr float kHeaveStiffnessScale = 3.0f;         // buoyancy slider -> heave spring firmness
    constexpr float kAngularStiffness    = 6.0f;         // pitch/roll spring stiffness
    constexpr float kCriticalDampFactor  = 2.0f;         // 2*zeta term for critical damping
    constexpr float kSlopeEps            = 1e-3f;        // guard tiny lever denominators
    constexpr float kMaxTiltRad          = 0.6f;         // clamp tilt (~34 deg) so waves can't flip
}

void BoatPhysics::step(float dt, const glm::vec3& fixedXZ, float yaw,
                       const std::function<float(float, float)>& heightAt) {
    // Navigation (XZ + yaw) is scripted by the caller; the physics solves the
    // vertical HEAVE + PITCH + ROLL from per-facet buoyancy forces. Pitch is
    // rotation about the boat's local X (forward) axis -> nose up/down; roll is
    // about local Z (beam) -> leaning side to side. We track them as scalar
    // angular state (stable, no gimbal gymnastics) and compose yaw*pitch*roll.
    if (!initialized) {
        position = glm::vec3(fixedXZ.x, heightAt(fixedXZ.x, fixedXZ.z), fixedXZ.z);
        pitch = roll = 0.0f;
        heaveVel = pitchVel = rollVel = 0.0f;
        initialized = true;
    }
    position.x = fixedXZ.x;
    position.z = fixedXZ.z;

    dt = std::clamp(dt, 0.0f, kMaxStepSeconds);

    // Current orientation = yaw, then pitch (local X), then roll (local Z).
    const glm::quat yawQ = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    const glm::quat ori  = glm::normalize(yawQ
                          * glm::angleAxis(pitch, glm::vec3(1, 0, 0))
                          * glm::angleAxis(roll,  glm::vec3(0, 0, 1)));
    const glm::mat3 R = glm::mat3_cast(ori);

    const float halfL = 0.5f * length;
    const float halfB = 0.5f * beam;
    const int   nx = std::max(2, facetsX);
    const int   nz = std::max(2, facetsZ);
    const float facetCount = float(nx * nz);

    // Sample the surface height at every facet; accumulate the average (for
    // heave) and the fore/aft + port/starboard weighted means (for pitch/roll).
    // A spring-damper toward these is far more stable than summing raw
    // Archimedes forces (which sink the boat unless perfectly tuned).
    float sumH = 0.0f;
    float fwdNum = 0.0f, fwdDen = 0.0f;   // height vs forward lever -> pitch
    float sideNum = 0.0f, sideDen = 0.0f; // height vs beam lever   -> roll

    for (int ix = 0; ix < nx; ++ix) {
        for (int iz = 0; iz < nz; ++iz) {
            float lx = -halfL + (2.0f * halfL) * (ix / float(nx - 1)); // forward (+X)
            float lz = -halfB + (2.0f * halfB) * (iz / float(nz - 1)); // beam (+Z)
            glm::vec3 worldOff = R * glm::vec3(lx, 0.0f, lz);
            glm::vec3 wp = position + worldOff;
            float surfH = heightAt(wp.x, wp.z);

            sumH    += surfH;
            fwdNum  += lx * surfH;  fwdDen  += lx * lx;
            sideNum += lz * surfH;  sideDen += lz * lz;
        }
    }
    float avgH = sumH / facetCount + floatHeight; // target waterline (+ freeboard)

    // --- HEAVE: critically-damped spring toward the average surface height ---
    // accel = stiffness*(target - y) - damping*vel  (stable, always floats).
    float stiffness = buoyancy * kHeaveStiffnessScale; // buoyancy slider scales firmness
    float heaveAccel = stiffness * (avgH - position.y) - kCriticalDampFactor * linearDamp * heaveVel;
    heaveVel += heaveAccel * dt;
    position.y += heaveVel * dt;

    // --- PITCH / ROLL: target angle from the surface slope across the hull ---
    // slope_fore = d(height)/d(forward); nose follows the water -> pitch = -slope.
    float slopeFwd  = (fwdDen  > kSlopeEps) ? fwdNum  / fwdDen  : 0.0f;
    float slopeSide = (sideDen > kSlopeEps) ? sideNum / sideDen : 0.0f;
    float targetPitch = -std::atan(slopeFwd);
    float targetRoll  =  std::atan(slopeSide);

    // critically-damped spring toward the target tilt (smooth, no sink)
    pitchVel += (kAngularStiffness * (targetPitch - pitch) - kCriticalDampFactor * angularDamp * pitchVel) * dt;
    rollVel  += (kAngularStiffness * (targetRoll  - roll)  - kCriticalDampFactor * angularDamp * rollVel)  * dt;
    pitch += pitchVel * dt;
    roll  += rollVel  * dt;

    // Clamp tilt so a violent wave can't flip the boat.
    pitch = std::clamp(pitch, -kMaxTiltRad, kMaxTiltRad);
    roll  = std::clamp(roll,  -kMaxTiltRad, kMaxTiltRad);

    orientation = glm::normalize(yawQ
                * glm::angleAxis(pitch, glm::vec3(1, 0, 0))
                * glm::angleAxis(roll,  glm::vec3(0, 0, 1)));
}
