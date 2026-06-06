#include "core/Window.h"
#include "core/Camera.h"
#include "graphics/Shader.h"
#include "water/WaterSimulation.h"
#include "water/RainSystem.h"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"


unsigned int loadCubemap(const std::vector<std::string>& faces) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    stbi_set_flip_vertically_on_load(false);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);

        if (data) {
            GLenum format = GL_RGB;
            if (nrChannels == 1) format = GL_RED;
            else if (nrChannels == 3) format = GL_RGB;
            else if (nrChannels == 4) format = GL_RGBA;

            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0,
                format,
                width,
                height,
                0,
                format,
                GL_UNSIGNED_BYTE,
                data
            );

            std::cout << "Loaded cubemap face: " << faces[i] << std::endl;
            stbi_image_free(data);
        } else {
            std::cout << "Cubemap tex failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

namespace {
    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr float kMaxAccumulatedTime = 0.25f;
    constexpr int kSurfaceChunkSize = 16;
    constexpr int kMaxLocalRipples = 128;
    constexpr float kRippleExpansionSpeed = 1.5f;
    constexpr float kRippleBandWidth = 0.2f;

    struct SurfaceChunk {
        unsigned int ebo = 0;
        GLsizei indexCount = 0;
        glm::vec2 minXZ = glm::vec2(0.0f);
        glm::vec2 maxXZ = glm::vec2(0.0f);
    };

    bool rippleAffectsChunk(const glm::vec3& ripple, const SurfaceChunk& chunk) {
        const float influenceRadius = ripple.z * kRippleExpansionSpeed + kRippleBandWidth;
        const float clampedX = std::max(chunk.minXZ.x, std::min(ripple.x, chunk.maxXZ.x));
        const float clampedZ = std::max(chunk.minXZ.y, std::min(ripple.y, chunk.maxXZ.y));
        const float dx = ripple.x - clampedX;
        const float dz = ripple.y - clampedZ;
        return (dx * dx + dz * dz) <= (influenceRadius * influenceRadius);
    }

    void uploadRipples(const Shader& shader, const std::vector<glm::vec3>& ripples) {
        shader.setInt("numRipples", static_cast<int>(ripples.size()));
        if (!ripples.empty()) {
            shader.setVec3Array("activeRipples", ripples.data(), static_cast<int>(ripples.size()));
        }
    }
}

int main();
