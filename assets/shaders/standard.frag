#version 460 core
out vec4 FragColor;

in vec3  FragPos;
in vec3  PhysicsNormal;
in vec2  OceanUV;
in vec2  DisturbUV;
in float SurfaceMask;

uniform vec3        viewPos;
uniform samplerCube skybox;
uniform sampler2D   normalMap;
uniform sampler2D   disturbanceMap;
uniform float       oceanSize;
uniform int         numRipples;

// Ripple data in SSBO — no constant-register limit (replaces uniform array)
layout(std430, binding = 4) readonly buffer RippleBuffer {
    vec4 ripples[]; // xy = world XZ, z = age, w unused
} rippleSSBO;
uniform float       time;

// normalize(vec3(0.5, 1.0, 0.3)) pre-computed — const normalize() is not guaranteed in all GL 4.6 drivers
const vec3 kSunDir = vec3(0.43193, 0.86386, 0.25932);

// ---- procedural mid-frequency normal detail ----
vec3 applyMediumWaveDetail(vec3 baseNormal, vec2 worldXZ) {
    float warp = 0.45 * sin(dot(worldXZ, vec2(-0.21, 0.98)) * 0.42 + time * 0.55)
               + 0.25 * sin(dot(worldXZ, vec2( 0.73, 0.31)) * 0.31 - time * 0.38);

    vec2 slope = vec2(0.0);
    slope += normalize(vec2( 0.86,  0.50)) * cos(dot(worldXZ, normalize(vec2( 0.86,  0.50))) * (6.2831853 / 10.5) + warp           + time * 1.05) * 0.052;
    slope += normalize(vec2(-0.48,  0.88)) * cos(dot(worldXZ, normalize(vec2(-0.48,  0.88))) * (6.2831853 /  6.3) - warp * 0.6     + time * 1.45) * 0.038;
    slope += normalize(vec2( 0.18, -0.98)) * cos(dot(worldXZ, normalize(vec2( 0.18, -0.98))) * (6.2831853 /  3.7) + warp * 0.35   + time * 2.05) * 0.022;
    slope += normalize(vec2(-0.96, -0.28)) * cos(dot(worldXZ, normalize(vec2(-0.96, -0.28))) * (6.2831853 / 18.0)                  - time * 0.72) * 0.030;

    return normalize(baseNormal + vec3(-slope.x, 0.0, -slope.y));
}

void main() {
    // ---- base water colour (depth-cued) ----
    vec3  deepColor  = vec3(0.004, 0.020, 0.075);
    vec3  midColor   = vec3(0.012, 0.080, 0.175);
    float heightFactor = clamp((FragPos.y + 1.5) * 0.4, 0.0, 1.0);
    vec3  albedo = mix(deepColor, midColor, heightFactor);

    // ---- normals ----
    vec3 finalNormal = normalize(PhysicsNormal);
    float foam = 0.0;

    if (SurfaceMask > 0.5) {
        vec4 normalSample = texture(normalMap, OceanUV);
        vec3 oceanNormal  = normalize(normalSample.xyz);
        foam = normalSample.a;

        // Physics normal offset (zero when PhysicsNormal is flat (0,1,0))
        vec3 physicsOffset = normalize(PhysicsNormal) - vec3(0.0, 1.0, 0.0);
        finalNormal = normalize(oceanNormal + physicsOffset);
        finalNormal = applyMediumWaveDetail(finalNormal, FragPos.xz);

        // ---- disturbance normal (C-key interactive waves) ----
        const float kTexel     = 1.0 / 256.0;
        const float kTexelWorld = oceanSize / 256.0; // metres per disturbance texel
        float dR = texture(disturbanceMap, DisturbUV + vec2(kTexel, 0.0)).r;
        float dL = texture(disturbanceMap, DisturbUV - vec2(kTexel, 0.0)).r;
        float dU = texture(disturbanceMap, DisturbUV + vec2(0.0, kTexel)).r;
        float dD = texture(disturbanceMap, DisturbUV - vec2(0.0, kTexel)).r;
        float slopeX = (dR - dL) / (2.0 * kTexelWorld);
        float slopeZ = (dU - dD) / (2.0 * kTexelWorld);
        finalNormal.x -= slopeX * 2.5;
        finalNormal.z -= slopeZ * 2.5;
        finalNormal = normalize(finalNormal);
    }

    // ---- rain ripple rings ----
    // Each ripple: xy = world XZ centre, z = age [0, kLifetime]
    // 4 rings ALL visible from spawn — fixed radial spacing, expand as one unit.
    const float kRingSpeed  = 5.0;   // m/s — fast snap outward
    const float kRingWidth  = 0.35;  // metres per ring
    const float kLifetime   = 6.0;   // seconds — long persistence
    const float kBurst      = 0.1;   // initial radius so all rings visible on frame 1
    const float kGap        = 0.5;   // metres between ring centres — tight cluster

    // k=3 outermost (leads), k=0 innermost; outer rings slightly stronger
    const float kAmp[4] = float[4](0.015, 0.030, 0.050, 0.070);

    for (int i = 0; i < numRipples; i++) {
        vec2  center = rippleSSBO.ripples[i].xy;
        float age    = rippleSSBO.ripples[i].z;
        float dist   = length(FragPos.xz - center);
        if (dist < 0.001) continue;

        // sqrt fade: holds near full strength most of the lifetime, gentle tail-off
        float fade = sqrt(max(0.0, 1.0 - age / kLifetime));
        vec2  dir  = normalize(FragPos.xz - center);

        for (int k = 0; k < 4; k++) {
            // All rings share same speed; k=3 leads by kGap*3 ahead of k=0
            float r = kBurst + age * kRingSpeed + float(k) * kGap;
            if (dist > r + kRingWidth || dist < r - kRingWidth) continue;

            float wave = sin((dist - r) * (3.14159265 / kRingWidth));
            finalNormal.x += dir.x * wave * kAmp[k] * fade;
            finalNormal.z += dir.y * wave * kAmp[k] * fade;
        }
    }
    finalNormal = normalize(finalNormal);

    // ---- lighting ----
    vec3 viewDir = normalize(viewPos - FragPos);

    // Fresnel (Schlick, R0 = 0.02 for water/air)
    float cosTheta = max(dot(viewDir, finalNormal), 0.0);
    float fresnel   = 0.02 + 0.98 * pow(1.0 - cosTheta, 5.0);

    // Cubemap reflection
    vec3 R = reflect(normalize(FragPos - viewPos), finalNormal);
    vec3 reflectionColor = texture(skybox, R).rgb;

    // Blinn-Phong specular (sun)
    vec3  halfDir = normalize(kSunDir + viewDir);
    float spec    = pow(max(dot(finalNormal, halfDir), 0.0), 512.0);
    vec3  specular = vec3(1.0, 0.97, 0.90) * spec * 2.8;

    // Subsurface scatter — light punching through wave crests toward viewer
    float scatter = max(0.0, dot(kSunDir, -finalNormal))
                  * max(0.0, dot(viewDir, kSunDir))
                  * heightFactor;
    vec3 scatterColor = vec3(0.02, 0.25, 0.18) * scatter * 2.0;

    // ---- compose ----
    float reflectStrength = SurfaceMask > 0.5 ? fresnel : 0.15;
    vec3 waterColor = mix(albedo + scatterColor, reflectionColor, reflectStrength) + specular;

    // Whitecap foam (Jacobian fold-over from normal map alpha)
    vec3 foamColor  = vec3(0.92, 0.96, 1.0);
    vec3 finalResult = mix(waterColor, foamColor, foam * 0.75);

    FragColor = vec4(finalResult, 1.0);
}
