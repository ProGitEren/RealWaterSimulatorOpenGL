#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 FragPos;
out vec3 Normal;
out vec2 UV;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    // inverse-transpose normal matrix so non-uniform scale (stretched mountains)
    // doesn't skew the normals
    Normal  = normalize(transpose(inverse(mat3(model))) * aNormal);
    UV      = aUV;
    gl_Position = projection * view * worldPos;
}
