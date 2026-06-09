#version 460 core
out vec4 FragColor;

in float vAlpha;
in float vSeed;

// Cheap value-noise so each foam blob has a churny, broken edge instead of a
// flat disc — reads as real sea foam rather than a soft dot.
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i), b = hash(i + vec2(1, 0));
    float c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    // gl_PointCoord is [0,1] across the sprite; centre it.
    vec2 uv = gl_PointCoord * 2.0 - 1.0;
    float r = length(uv);
    if (r > 1.0) discard;

    // Soft round falloff from the centre.
    float core = smoothstep(1.0, 0.1, r);

    // Break up the disc with noise so the foam looks churned and patchy.
    float n = noise(uv * 3.5 + vSeed * 31.7);
    float foam = core * (0.55 + 0.7 * n);
    foam = smoothstep(0.18, 0.9, foam);

    // Slightly blue-white, brighter in the core.
    vec3 color = mix(vec3(0.78, 0.86, 0.92), vec3(1.0), core);

    FragColor = vec4(color, foam * vAlpha * 0.85);
}
