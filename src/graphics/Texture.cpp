#include "Texture.h"
#include <glad/glad.h>
#include "stb/stb_image.h"   // implementation is provided in main.cpp
#include <iostream>

unsigned int loadTexture2D(const std::string& path, bool srgb) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "Texture: failed to load " << path << std::endl;
        return 0;
    }

    GLenum srcFormat = GL_RGB;
    GLenum intFormat = srgb ? GL_SRGB8 : GL_RGB8;
    if (channels == 1)      { srcFormat = GL_RED;  intFormat = GL_R8; }
    else if (channels == 3) { srcFormat = GL_RGB;  intFormat = srgb ? GL_SRGB8        : GL_RGB8; }
    else if (channels == 4) { srcFormat = GL_RGBA; intFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8; }

    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(intFormat), width, height, 0,
                 srcFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    return id;
}
