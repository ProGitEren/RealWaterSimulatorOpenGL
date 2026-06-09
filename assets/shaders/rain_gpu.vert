#version 460 core
// Instanced/attributeless rain: no vertex buffer. We draw uCount*2 vertices as
// GL_LINES; each pair of vertices = one drop's streak (tip + tail), read straight
// from the GPU drop SSBO. Zero CPU vertex work, one draw call.
struct Drop { vec4 pos; vec4 vel; };
layout(std430, binding = 0) readonly buffer DropBuffer { Drop drops[]; };

uniform mat4  projection;
uniform mat4  view;
uniform vec3  uWindDrift;
uniform float uDropSize;

out float vAlpha;

void main() {
    uint drop = uint(gl_VertexID) >> 1;        // 2 verts per drop
    bool tail = (gl_VertexID & 1) == 1;        // 0 = bright tip, 1 = faded tail

    Drop d = drops[drop];
    vec3 p = d.pos.xyz;

    // Streak points along the drop's velocity (down + wind), length ~ speed.
    float len = d.pos.w * 0.18 * uDropSize;
    vec3  dir = normalize(vec3(uWindDrift.x, -d.pos.w, uWindDrift.y));

    if (tail) { p -= dir * len; vAlpha = 0.0; }
    else      {                 vAlpha = 1.0; }

    gl_Position = projection * view * vec4(p, 1.0);
}
