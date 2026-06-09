#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aSize;   // world-space foam patch radius (m)
layout (location = 2) in float aAlpha;
layout (location = 3) in float aSeed;

uniform mat4 projection;
uniform mat4 view;
uniform vec3 viewPos;

out float vAlpha;
out float vSeed;

void main() {
    vec4 viewPos4 = view * vec4(aPos, 1.0);
    gl_Position = projection * viewPos4;

    // Point size in pixels ~ worldSize * focal / distance. Approximate with the
    // view-space depth; clamp so near foam isn't gigantic and far foam stays visible.
    float dist = max(-viewPos4.z, 0.1);
    gl_PointSize = clamp(aSize * 900.0 / dist, 2.0, 110.0);

    // Distance fade so far wakes don't pop.
    float distFade = clamp(1.0 - (dist - 400.0) / 600.0, 0.0, 1.0);
    vAlpha = aAlpha * distFade;
    vSeed  = aSeed;
}
