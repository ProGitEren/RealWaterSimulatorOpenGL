#version 460 core
out vec4 FragColor;
in float vAlpha;

void main() {
    // Bright blue-white water column, mostly independent of rain opacity so the
    // splash reads as water kicked off the surface.
    FragColor = vec4(vec3(0.85, 0.92, 1.0), vAlpha * 0.6);
}
