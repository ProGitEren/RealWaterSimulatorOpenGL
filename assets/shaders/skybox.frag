#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

// samplerCube is a special OpenGL type for Skyboxes!
uniform samplerCube skybox;

void main() {    
    FragColor = texture(skybox, TexCoords);
}