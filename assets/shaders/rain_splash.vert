#version 460 core
// GPU splash jets: a brief vertical water column kicked up where a drop hits.
// Driven by the SAME drop SSBO. The compute shader writes a splash timer into
// vel.x (seconds remaining, counting down) when a drop hits the water; here we
// draw a 2-vertex column (base on the surface → rising tip) for active splashes.
struct Drop { vec4 pos; vec4 vel; };
layout(std430, binding = 0) readonly buffer DropBuffer { Drop drops[]; };

uniform mat4  projection;
uniform mat4  view;
uniform float uSplashHeight;

out float vAlpha;

void main() {
    uint  drop  = uint(gl_VertexID) >> 1;
    bool  tip   = (gl_VertexID & 1) == 0;   // 0 = rising tip, 1 = base at surface
    Drop  d     = drops[drop];

    float splashT = d.vel.x;                 // seconds of splash remaining
    float seed    = d.vel.w;

    // No active splash -> emit a degenerate (zero-length, fully transparent) line.
    if (splashT <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vAlpha = 0.0; return; }

    // Parabolic rise+fall over a short life (splashT counts down from kSplashLife).
    const float kSplashLife    = 0.45; // MUST match rain_update.comp
    const float kWaterSurfaceY = 0.5;  // water plane Y (must match rain_update.comp + the FFT base)
    float age  = kSplashLife - splashT;                 // 0..kSplashLife
    float t    = clamp(age / kSplashLife, 0.0, 1.0);
    float h    = (1.0 - (2.0 * t - 1.0) * (2.0 * t - 1.0)) // parabola peaks mid-life
               * (1.5 + seed * 2.0) * uSplashHeight;

    vec3 base = vec3(d.vel.y, kWaterSurfaceY, d.vel.z);   // impact XZ (stored at hit time)
    vec3 p    = tip ? base + vec3(0.0, h, 0.0) : base;
    vAlpha    = tip ? (1.0 - t) : 0.0;       // bright tip fading, transparent base

    gl_Position = projection * view * vec4(p, 1.0);
}
