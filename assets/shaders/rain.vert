#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aAlpha; // 1=tip, 0=tail

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 viewPos;

const vec3 kSunDir  = vec3(0.43193, 0.86386, 0.25932);
uniform vec3 kDropDir; // normalized fall + wind direction (set from CPU)

out float vAlpha;
out vec3  vColor;

void main() {
    // ---- distance fade: sharp at 30m, gone at 200m ----
    float dist     = length(aPos - viewPos);
    float distFade = clamp(1.0 - (dist - 30.0) / 170.0, 0.0, 1.0);

    // ---- view-angle fade: drops nearly invisible when looked at end-on ----
    vec3  toView   = normalize(viewPos - aPos);
    float viewPerp = clamp(1.0 - abs(dot(toView, kDropDir)), 0.0, 1.0);

    // ---- cylinder specular: glint when sun hits the drop at the right angle ----
    vec3 perpSun  = normalize(kSunDir - dot(kSunDir, kDropDir) * kDropDir);
    vec3 perpView = normalize(toView  - dot(toView,  kDropDir) * kDropDir);
    float spec    = pow(max(dot(perpView, perpSun), 0.0), 24.0);

    // ---- color: bright silver-white (sky light reflected off water drops) ----
    // Strong ambient sky component keeps drops light against dark water
    // They appear near-white on water, near-transparent on sky — correct for rain
    vec3 skyAmbient = vec3(0.80, 0.84, 0.90);
    vColor = skyAmbient + vec3(1.0, 0.97, 0.93) * spec * 0.8;

    vAlpha = aAlpha * distFade * viewPerp;

    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
