#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 projection;
uniform mat4 view;

void main() {
    TexCoords = aPos;
    vec4 pos = projection * view * vec4(aPos, 1.0);
    // .xyww forces post-perspective-divide depth to 1.0 (the far plane) so the
    // skybox renders behind all geometry; pairs with glDepthFunc(GL_LEQUAL).
    gl_Position = pos.xyww;
}
