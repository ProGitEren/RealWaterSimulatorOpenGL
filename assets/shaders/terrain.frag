#version 460 core
out vec4 FragColor;

in vec3  FragPos;
in vec3  Normal;
in float Height;

uniform vec3  viewPos;
uniform float maxHeight;

// Tiling PBR material set: rock (cliffs), grass (low slopes), snow (peaks).
// Each has a diffuse + normal map. `hasTextures` switches between the textured
// path and the procedural-colour fallback so the scene still renders if the
// textures haven't been downloaded yet.
uniform sampler2D rockDiff,  rockNorm;
uniform sampler2D grassDiff, grassNorm;
uniform sampler2D snowDiff,  snowNorm;
uniform int   hasTextures;  // rock texture present -> use triplanar rock
uniform int   hasGrass;     // grass texture present
uniform int   hasSnow;      // snow texture present
uniform float texScale;     // world metres per texture tile

const vec3 kSunDir = vec3(0.43193, 0.86386, 0.25932);

// --- triplanar sampling: project the texture along x/y/z and blend by the
//     surface normal, so steep cliffs don't show stretched texels ---
vec3 triplanar(sampler2D tex, vec3 worldPos, vec3 n, float scale) {
    vec3 blend = abs(n);
    blend = pow(blend, vec3(4.0));
    blend /= (blend.x + blend.y + blend.z);
    vec3 cx = texture(tex, worldPos.yz / scale).rgb;
    vec3 cy = texture(tex, worldPos.xz / scale).rgb;
    vec3 cz = texture(tex, worldPos.xy / scale).rgb;
    return cx * blend.x + cy * blend.y + cz * blend.z;
}

void main() {
    vec3 N = normalize(Normal);
    float flatness = clamp(dot(N, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
    float e = clamp(Height / max(maxHeight, 0.001), 0.0, 1.0);

    vec3 albedo;

    if (hasTextures == 1) {
        // Rock everywhere via triplanar (no UV stretch on cliffs). Sample at two
        // scales and mix to break up obvious tiling on the big mountains.
        vec3 rockC = mix(triplanar(rockDiff, FragPos, N, texScale),
                         triplanar(rockDiff, FragPos, N, texScale * 0.27),
                         0.5);
        albedo = rockC;

        // Grass on gentle low slopes (textured if available, else a green tint).
        float grassW = smoothstep(0.16, 0.05, e) * smoothstep(0.55, 0.82, flatness);
        vec3  grassC = (hasGrass == 1) ? triplanar(grassDiff, FragPos, N, texScale * 0.5)
                                       : vec3(0.20, 0.30, 0.15);
        albedo = mix(albedo, grassC, grassW);

        // Snow caps on high, flatter ground (textured if available, else white).
        float snowW = smoothstep(0.6, 0.82, e) * smoothstep(0.45, 0.8, flatness);
        vec3  snowC = (hasSnow == 1) ? triplanar(snowDiff, FragPos, N, texScale)
                                     : vec3(0.92, 0.94, 0.98);
        albedo = mix(albedo, snowC, snowW);

        // Fine surface detail from the rock normal map.
        vec3 rn = triplanar(rockNorm, FragPos, N, texScale) * 2.0 - 1.0;
        N = normalize(N + rn * 0.4);
    } else {
        // --- procedural fallback (no textures downloaded yet) ---
        vec3 sand  = vec3(0.45, 0.40, 0.30);
        vec3 grass = vec3(0.20, 0.32, 0.16);
        vec3 rock  = vec3(0.34, 0.31, 0.28);
        vec3 snow  = vec3(0.92, 0.94, 0.98);
        albedo = sand;
        albedo = mix(albedo, grass, smoothstep(0.02, 0.12, e));
        albedo = mix(albedo, rock,  smoothstep(0.20, 0.45, e));
        albedo = mix(albedo, snow,  smoothstep(0.70, 0.88, e));
        albedo = mix(rock, albedo, smoothstep(0.45, 0.75, flatness));
    }

    // --- lighting ---
    // Diffuse maps were uploaded as sRGB, so the GPU gives us LINEAR albedo here.
    // Light in linear, then encode back to sRGB at the end — otherwise the
    // darkened linear values go straight to screen and look muddy.
    float diff = max(dot(N, kSunDir), 0.0);
    vec3 ambient = vec3(0.55, 0.58, 0.62);   // brighter sky fill so it isn't dark
    vec3 lit = albedo * (ambient + diff * 1.1);

    float dist = length(viewPos - FragPos);
    float haze = smoothstep(800.0, 3000.0, dist);
    lit = mix(lit, vec3(0.62, 0.70, 0.80), haze * 0.5);

    // linear -> sRGB gamma encode
    lit = pow(lit, vec3(1.0 / 2.2));

    FragColor = vec4(lit, 1.0);
}
