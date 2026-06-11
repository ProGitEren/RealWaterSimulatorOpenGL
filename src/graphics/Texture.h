#ifndef TEXTURE_H
#define TEXTURE_H

#include <string>

// Loads a 2D texture from an image file via stb_image and uploads it to GL.
// `srgb` should be true for color/albedo maps, false for data maps (normal,
// roughness, AO) so the GPU does correct gamma handling.
// Returns the GL texture id, or 0 on failure.
unsigned int loadTexture2D(const std::string& path, bool srgb);

#endif
