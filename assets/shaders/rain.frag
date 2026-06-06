#version 460 core
out vec4 FragColor;

in float vAlpha;
in vec3  vColor;

void main() {
    // Very transparent — real rain drops are barely perceptible individually
    FragColor = vec4(vColor, vAlpha * 0.22);
}
