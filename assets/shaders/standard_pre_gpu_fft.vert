#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 FragPos;
out vec3 Normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float time;

vec3 GerstnerWave(vec4 wave, vec3 p, inout vec3 tangent, inout vec3 binormal) {
    float steepness = wave.z;
    float wavelength = wave.w;
    float k = 2.0 * 3.14159 / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(wave.xy);
    float f = k * (dot(d, p.xz) - c * time);
    float a = steepness / k;

    tangent += vec3(
        -d.x * d.x * (steepness * sin(f)),
        d.x * (steepness * cos(f)),
        -d.x * d.y * (steepness * sin(f))
    );
    binormal += vec3(
        -d.x * d.y * (steepness * sin(f)),
        d.y * (steepness * cos(f)),
        -d.y * d.y * (steepness * sin(f))
    );

    return vec3(
        d.x * (a * cos(f)),
        a * sin(f),
        d.y * (a * cos(f))
    );
}

void main() {
    vec3 p = aPos;
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);
    vec3 wavePos = vec3(aPos.x, 0.0, aPos.z);

    vec4 wave1 = vec4(1.0, 0.5, 0.15, 12.0);
    vec4 wave2 = vec4(0.8, -0.6, 0.2, 5.0);
    vec4 wave3 = vec4(-0.2, 0.3, 0.15, 2.5);

    p += GerstnerWave(wave1, wavePos, tangent, binormal);
    p += GerstnerWave(wave2, wavePos, tangent, binormal);
    p += GerstnerWave(wave3, wavePos, tangent, binormal);

    vec3 oceanNormal = normalize(cross(binormal, tangent));
    vec3 finalLocalNormal = normalize(oceanNormal + vec3(aNormal.x, 0.0, aNormal.z));

    FragPos = vec3(model * vec4(p, 1.0));
    Normal = mat3(transpose(inverse(model))) * finalLocalNormal;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
