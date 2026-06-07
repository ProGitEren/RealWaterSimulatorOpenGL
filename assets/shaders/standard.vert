#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 FragPos;
out vec3 PhysicsNormal;
out vec2 OceanUV;
out vec2 DisturbUV;   // world-space UV into disturbance texture (non-tiling)
out float SurfaceMask;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform sampler2D displacementMap;
uniform sampler2D disturbanceMap;
uniform float oceanSize;
uniform float floorY;
uniform int applyOceanDisplacement;

void main() {
    PhysicsNormal = normalize(aNormal);

    // World position before any displacement
    vec3 worldPos = vec3(model * vec4(aPos, 1.0));

    // FFT UV: world-anchored, one FFT patch per oceanSize. The 512-res Stockham
    // FFT supplies all the fine detail, so no tex_coord_scale multiplier is
    // needed (a >1 scale broke tileability at the patch edge -> seam line).
    // Continuous UVs (no fract) let GL_REPEAT wrap cleanly per-fragment.
    OceanUV = (worldPos.xz / oceanSize + vec2(0.5));

    // Disturbance UV: non-tiling, world-anchored [0,1]
    DisturbUV = clamp(worldPos.xz / oceanSize + vec2(0.5), vec2(0.0), vec2(1.0));

    SurfaceMask = (applyOceanDisplacement != 0 && aPos.y > floorY + 1.0) ? 1.0 : 0.0;

    if (SurfaceMask > 0.5) {
        // FFT ocean displacement
        vec3 disp = texture(displacementMap, OceanUV).xyz;
        worldPos += disp;

        // Interactive disturbance layer (C-key waves)
        worldPos.y += textureLod(disturbanceMap, DisturbUV, 0.0).r;
    }

    FragPos = worldPos;
    gl_Position = projection * view * vec4(worldPos, 1.0);
}
