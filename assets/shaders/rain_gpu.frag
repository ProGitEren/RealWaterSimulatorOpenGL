#version 460 core
out vec4 FragColor;
in float vAlpha;

uniform float uOpacity;

void main() {
    // Silver-white rain, faint; tip bright -> tail transparent (vAlpha).
    FragColor = vec4(vec3(0.80, 0.84, 0.90), vAlpha * uOpacity);
}
