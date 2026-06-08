#ifndef ROCK_GENERATOR_H
#define ROCK_GENERATOR_H

#include "Mesh.h"
#include <vector>

// Generates a rough rock mesh: a subdivided icosphere with value-noise
// displacement applied to each vertex, then recomputed normals. Deterministic
// per seed, so the same seed always yields the same rock.
//
// `subdivisions` controls smoothness (2-3 is a good rock). `seed` varies shape.
void generateRock(unsigned int subdivisions, unsigned int seed,
                  std::vector<Vertex>& outVertices,
                  std::vector<unsigned int>& outIndices);

#endif
