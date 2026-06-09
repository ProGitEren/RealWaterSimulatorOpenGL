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
uniform float       uRippleLifetime;  // rain ripple ring lifetime (s)
uniform float       uRingSpeed;       // ring expansion speed (m/s)
uniform float       uRingStrength;    // ring distortion multiplier

// Ripple data in SSBO — no constant-register limit (replaces uniform array)
layout(std430, binding = 4) readonly buffer RippleBuffer {
    vec4 ripples[]; // xy = world XZ, z = age, w unused
} rippleSSBO;
uniform float       time;

// --- Look / lighting knobs (UI-tunable) ---
uniform vec3  uDeepColor;
uniform vec3  uShallowColor;
uniform float uDepthFalloff;
uniform float uReflectStrength;
uniform vec3  uHorizonColor;
uniform vec3  uScatterColor;
uniform vec3  uSunColor;
uniform vec3  uSunDir;       // normalized; from sun azimuth/elevation
uniform float uSunGlint;
uniform float uSunGlitter;
uniform float uExposure;
uniform float uMidWaveDetail;

// Well-known constants (exact digits preserved from the original literals).
const float TWO_PI = 6.2831853;
const float PI     = 3.14159265;

// ---- procedural mid-frequency normal detail ----
vec3 applyMediumWaveDetail(vec3 baseNormal, vec2 worldXZ) {
    float warp = 0.45 * sin(dot(worldXZ, vec2(-0.21, 0.98)) * 0.42 + time * 0.55)
               + 0.25 * sin(dot(worldXZ, vec2( 0.73, 0.31)) * 0.31 - time * 0.38);

    vec2 slope = vec2(0.0);
    slope += normalize(vec2( 0.86,  0.50)) * cos(dot(worldXZ, normalize(vec2( 0.86,  0.50))) * (TWO_PI / 10.5) + warp           + time * 1.05) * 0.052;
    slope += normalize(vec2(-0.48,  0.88)) * cos(dot(worldXZ, normalize(vec2(-0.48,  0.88))) * (TWO_PI /  6.3) - warp * 0.6     + time * 1.45) * 0.038;
    slope += normalize(vec2( 0.18, -0.98)) * cos(dot(worldXZ, normalize(vec2( 0.18, -0.98))) * (TWO_PI /  3.7) + warp * 0.35   + time * 2.05) * 0.022;
    slope += normalize(vec2(-0.96, -0.28)) * cos(dot(worldXZ, normalize(vec2(-0.96, -0.28))) * (TWO_PI / 18.0)                  - time * 0.72) * 0.030;

    return normalize(baseNormal + vec3(-slope.x, 0.0, -slope.y) * uMidWaveDetail);
}

void main() {
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
        const float kDisturbResolution = 256.0; // must match GPUDisturbance(256u, ...) in main.cpp
        const float kTexel      = 1.0 / kDisturbResolution;
        const float kTexelWorld = oceanSize / kDisturbResolution; // metres per disturbance texel
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
    const float kRingWidth  = 0.35;  // metres per ring
    float       kLifetime   = uRippleLifetime;  // seconds (UI)
    const float kBurst      = 0.1;   // initial radius so all rings visible on frame 1
    const float kGap        = 0.5;   // metres between ring centres — tight cluster

    // k=3 outermost (leads), k=0 innermost; outer rings slightly stronger
    const float kAmp[4] = float[4](0.015, 0.030, 0.050, 0.070);

    // PERF: a single ripple only ever reaches ~kBurst + maxReach + 3*kGap + width
    // metres. The vast majority of the up-to-512 ripples are nowhere near any
    // given fragment, so we reject them with a cheap squared-distance test BEFORE
    // the expensive normalize()/sin() and the inner ring loop. This is mathematically
    // identical (skipped ripples contribute exactly 0) but turns the per-pixel cost
    // from O(512*4) into "only the handful of nearby ripples" — the difference
    // between smooth and "stuck" on weaker (e.g. Windows laptop) GPUs.
    const float kExpandTime = 0.7;   // seconds to approach full spread
    // Largest reach any ripple can have (age -> inf => factor -> 1).
    const float kMaxReach   = uRingSpeed * kExpandTime;
    const float kRippleMax  = kBurst + kMaxReach + 3.0 * kGap + kRingWidth;
    const float kRippleMax2 = kRippleMax * kRippleMax;

    // Also skip ripples far from the camera — their normal perturbation is
    // invisible at distance and only burns cycles. (Pure optimisation, no look change.)
    float fragCamDist2 = dot(FragPos.xz - viewPos.xz, FragPos.xz - viewPos.xz);
    bool  rippleVisibleHere = fragCamDist2 < (350.0 * 350.0);

    if (rippleVisibleHere) {
        for (int i = 0; i < numRipples; i++) {
            vec2  center = rippleSSBO.ripples[i].xy;
            vec2  d2v    = FragPos.xz - center;
            float dist2  = dot(d2v, d2v);
            // EARLY OUT: outside this ripple's maximum footprint -> contributes 0.
            if (dist2 > kRippleMax2 || dist2 < 1e-6) continue;

            float dist = sqrt(dist2);
            float age  = rippleSSBO.ripples[i].z;

            float life = clamp(age / kLifetime, 0.0, 1.0);
            float fade = 1.0 - smoothstep(0.55, 1.0, life);
            vec2  dir  = d2v / dist;   // = normalize(), but reuse the sqrt we just did

            float reach = kMaxReach * (1.0 - exp(-age / kExpandTime));

            for (int k = 0; k < 4; k++) {
                float r = kBurst + reach + float(k) * kGap;
                if (dist > r + kRingWidth || dist < r - kRingWidth) continue;

                float wave = sin((dist - r) * (PI / kRingWidth));
                finalNormal.x += dir.x * wave * kAmp[k] * uRingStrength * fade;
                finalNormal.z += dir.y * wave * kAmp[k] * uRingStrength * fade;
            }
        }
    }
    finalNormal = normalize(finalNormal);

    // ---- lighting (achalpandeyy/OceanFFT HDR-fresnel model) ----
    // The crisp high-contrast ocean look comes from lighting in HDR (sky colour
    // values well above 1.0) and tonemapping at the end. Fresnel drives the
    // whole thing: slopes facing you stay dark ocean blue, slopes glancing the
    // sky pick up sky light — that contrast is what makes each wave read sharply.
    //
    // This is a full physically-based deep-ocean model:
    //   reflection (real cubemap) + refraction (depth-tinted body) +
    //   subsurface scatter (backlit crest glow) + sun specular glitter +
    //   foam, all composited in HDR and tonemapped.
    vec3  viewDir  = normalize(viewPos - FragPos);
    vec3  incident = normalize(FragPos - viewPos);
    float NdotV    = max(dot(finalNormal, viewDir), 0.0);

    // ---- Fresnel (Schlick, R0 = 0.02 for water/air) ----
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    // ---- REFLECTION: real environment, from your skybox cubemap ----
    // Reflect the view ray off the wave normal and sample the actual sky. At
    // grazing crest angles the ray can dip below the horizon; blend toward a
    // bright horizon-sky tint there so we never sample the dark cube underside.
    vec3  R            = reflect(incident, finalNormal);
    // Boost the LDR cubemap into HDR so the exposure tonemap below renders the
    // sky reflection at its true brightness instead of crushing it dark.
    vec3  envReflect   = texture(skybox, R).rgb * uReflectStrength;
    float belowHorizon = smoothstep(0.0, -0.2, R.y);
    vec3  reflection   = mix(envReflect, uHorizonColor, belowHorizon * 0.7);

    // ---- REFRACTION: depth-tinted water body ----
    // Deep troughs = dark navy (long optical path), crests = brighter teal.
    float depthCue   = clamp((FragPos.y + 2.5) * uDepthFalloff, 0.0, 1.0);
    vec3  bodyColor  = mix(uDeepColor, uShallowColor, depthCue);

    // Sun diffuse lifts the lit faces of the body a touch.
    float diffuse = clamp(dot(finalNormal, uSunDir), 0.0, 1.0);
    bodyColor *= (0.5 + 0.5 * diffuse);

    // ---- SUBSURFACE SCATTER: the signature "glow through the wave" ----
    // Light transmitted through a crest toward the eye — strongest when the sun
    // is behind the wave and you're looking roughly toward the sun. Keyed on
    // crest height so it appears on the tops of swells, not in flat troughs.
    float crest    = clamp((FragPos.y - 0.1) * 0.4, 0.0, 1.0);
    float backlight = pow(max(0.0, dot(viewDir, -uSunDir)), 4.0);
    float sideLight = max(0.0, dot(finalNormal, uSunDir)) * 0.5 + 0.5;
    vec3  scatter   = uScatterColor * crest * backlight * sideLight * 2.2;

    // ---- COMPOSE refraction + scatter (under) with reflection (over) ----
    vec3 underwater = bodyColor + scatter;
    float reflectStrength = SurfaceMask > 0.5 ? fresnel : 0.15;
    vec3 color = mix(underwater, reflection, reflectStrength);

    // ---- SUN SPECULAR: tight glint + broad glitter path ----
    vec3  halfDir = normalize(uSunDir + viewDir);
    float NdotH   = max(dot(finalNormal, halfDir), 0.0);
    float glint   = pow(NdotH, 1200.0);   // sharp mirror highlight
    float glitter = pow(NdotH, 120.0);    // broad sparkle over the chop
    color += uSunColor * (glint * uSunGlint + glitter * uSunGlitter);

    // ---- FOAM: wave-crest whitecaps (FFT Jacobian) ----
    float foamMask  = smoothstep(0.05, 0.55, foam);
    float foamLight = 0.7 + 0.3 * diffuse;
    vec3  foamCol   = vec3(0.95, 0.98, 1.02) * foamLight;
    color = mix(color, foamCol, foamMask * 0.85);

    // ---- DISTANCE HAZE: soften far water into the sky at the horizon ----
    float dist = length(viewPos - FragPos);
    float haze = smoothstep(500.0, 1700.0, dist);
    color = mix(color, uHorizonColor, haze * 0.55);

    // ---- HDR tonemap (exposure) for crisp contrast instead of a flat clamp ----
    vec3 finalResult = 1.0 - exp(-color * uExposure);

    FragColor = vec4(finalResult, 1.0);
}
