#version 460 core
out vec4 FragColor;

in float vAlpha;
in vec3  vColor;

uniform float uOpacity;   // overall rain opacity (UI)
uniform int   uIsSplash;  // 1 = splash water column (bright), 0 = rain streak

void main() {
    if (uIsSplash == 1) {
        // Splash jet: bright blue-white, mostly independent of the rain opacity
        // so the height is always readable as water kicked off the surface.
        FragColor = vec4(vec3(0.85, 0.92, 1.0), vAlpha * 0.6);
    } else {
        FragColor = vec4(vColor, vAlpha * uOpacity);
    }
}
