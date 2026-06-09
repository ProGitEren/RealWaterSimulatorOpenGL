#include "Model.h"
#include "Texture.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <map>
#include <functional>

Model::Model(const std::string& path) {
    loadFromFile(path);
}

Model::Model(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    m_meshes.emplace_back(vertices, indices);
}

glm::mat4 Model::modelMatrix() const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, m_position);
    m *= glm::mat4_cast(m_orientation);     // full orientation (was rotateY only)
    m = glm::scale(m, m_scale);
    return m;
}

void Model::draw(const Shader& shader) const {
    shader.setMat4("model", modelMatrix());
    for (const Mesh& mesh : m_meshes) {
        const Material& m = mesh.material;

        // Bind material maps to texture units 4..7 (0-3 are used by the ocean's
        // skybox/displacement/normal/disturbance; object.frag uses skybox on 0).
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m.albedo);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m.normalMap);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, m.roughness);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m.ao);

        shader.setInt("albedoMap", 4);
        shader.setInt("normalMap", 5);
        shader.setInt("roughnessMap", 6);
        shader.setInt("aoMap", 7);
        shader.setInt("hasAlbedo",   m.albedo    != 0 ? 1 : 0);
        shader.setInt("hasNormalMap", m.normalMap != 0 ? 1 : 0);
        shader.setInt("hasAO",       m.ao        != 0 ? 1 : 0);
        shader.setVec3("baseColor",  m.baseColor); // used when albedo == 0

        mesh.draw();
    }
}

// --- glTF loading via cgltf ------------------------------------------------

namespace {
    // Reads a float accessor (vec2/vec3) element-by-element into a flat buffer.
    void readFloats(const cgltf_accessor* accessor, std::vector<float>& out, int components) {
        if (!accessor) return;
        const cgltf_size count = accessor->count;
        out.resize(count * components);
        for (cgltf_size i = 0; i < count; ++i) {
            cgltf_accessor_read_float(accessor, i, &out[i * components], components);
        }
    }

    // Directory portion of a path (so relative texture URIs can be resolved).
    std::string directoryOf(const std::string& path) {
        size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }

    // Loads a glTF texture (external file URI) once, caching by image pointer so
    // shared textures aren't uploaded twice. Embedded textures are skipped for
    // now (Poly Haven glTF uses external .jpg files). srgb=true for color maps.
    unsigned int loadGltfTexture(const cgltf_texture* tex, const std::string& dir,
                                 bool srgb, std::map<const cgltf_image*, unsigned int>& cache) {
        if (!tex || !tex->image) return 0;
        const cgltf_image* img = tex->image;
        auto it = cache.find(img);
        if (it != cache.end()) return it->second;

        unsigned int id = 0;
        if (img->uri && img->uri[0] != '\0') {
            // External file (could be percent-encoded; cgltf leaves it as-is for
            // simple names which Poly Haven uses).
            std::string full = dir + img->uri;
            id = loadTexture2D(full, srgb);
        }
        cache[img] = id;
        return id;
    }
}

void Model::loadFromFile(const std::string& path) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        std::cerr << "Model: failed to parse " << path << " (cgltf error " << result << ")" << std::endl;
        return;
    }

    result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        std::cerr << "Model: failed to load buffers for " << path << std::endl;
        cgltf_free(data);
        return;
    }

    const std::string dir = directoryOf(path);
    std::map<const cgltf_image*, unsigned int> texCache;

    // Process one primitive, baking the node's world matrix into the vertices
    // (so multi-part models like the yacht/ship assemble correctly).
    auto processPrimitive = [&](const cgltf_primitive& prim, const glm::mat4& world) {
        if (prim.type != cgltf_primitive_type_triangles) return;

        const cgltf_accessor* posAcc = nullptr;
        const cgltf_accessor* nrmAcc = nullptr;
        const cgltf_accessor* uvAcc  = nullptr;
        for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
            const cgltf_attribute& attr = prim.attributes[ai];
            if (attr.type == cgltf_attribute_type_position) posAcc = attr.data;
            else if (attr.type == cgltf_attribute_type_normal) nrmAcc = attr.data;
            else if (attr.type == cgltf_attribute_type_texcoord && !uvAcc) uvAcc = attr.data;
        }
        if (!posAcc) return;

        const glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(world)));

        std::vector<float> pos, nrm, uv;
        readFloats(posAcc, pos, 3);
        readFloats(nrmAcc, nrm, 3);
        readFloats(uvAcc,  uv,  2);

        const size_t vertexCount = posAcc->count;
        std::vector<Vertex> vertices(vertexCount);
        for (size_t v = 0; v < vertexCount; ++v) {
            glm::vec3 p(pos[v * 3 + 0], pos[v * 3 + 1], pos[v * 3 + 2]);
            vertices[v].position = glm::vec3(world * glm::vec4(p, 1.0f));    // bake transform
            m_aabbMin = glm::min(m_aabbMin, vertices[v].position);
            m_aabbMax = glm::max(m_aabbMax, vertices[v].position);
            glm::vec3 n = nrm.empty() ? glm::vec3(0.0f, 1.0f, 0.0f)
                                      : glm::vec3(nrm[v * 3 + 0], nrm[v * 3 + 1], nrm[v * 3 + 2]);
            vertices[v].normal = glm::normalize(normalMat * n);
            vertices[v].uv     = uv.empty() ? glm::vec2(0.0f)
                                            : glm::vec2(uv[v * 2 + 0], uv[v * 2 + 1]);
        }

        std::vector<unsigned int> indices;
        if (prim.indices) {
            indices.resize(prim.indices->count);
            for (cgltf_size idx = 0; idx < prim.indices->count; ++idx)
                indices[idx] = static_cast<unsigned int>(cgltf_accessor_read_index(prim.indices, idx));
        } else {
            indices.resize(vertexCount);
            for (size_t v = 0; v < vertexCount; ++v) indices[v] = static_cast<unsigned int>(v);
        }

        m_meshes.emplace_back(vertices, indices);
        Mesh& addedMesh = m_meshes.back();

        if (prim.material) {
            const cgltf_material* mat = prim.material;
            if (mat->has_pbr_metallic_roughness) {
                const auto& pbr = mat->pbr_metallic_roughness;
                addedMesh.material.albedo =
                    loadGltfTexture(pbr.base_color_texture.texture, dir, true, texCache);
                addedMesh.material.roughness =
                    loadGltfTexture(pbr.metallic_roughness_texture.texture, dir, false, texCache);
                addedMesh.material.baseColor = glm::vec3(
                    pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
            }
            addedMesh.material.normalMap =
                loadGltfTexture(mat->normal_texture.texture, dir, false, texCache);
            addedMesh.material.ao =
                loadGltfTexture(mat->occlusion_texture.texture, dir, false, texCache);
        }
    };

    // Recursively walk the node tree, accumulating world transforms.
    std::function<void(const cgltf_node*, const glm::mat4&)> visit =
        [&](const cgltf_node* node, const glm::mat4& parent) {
            glm::mat4 local(1.0f);
            cgltf_node_transform_local(node, &local[0][0]); // TRS or matrix -> mat4
            glm::mat4 world = parent * local;
            if (node->mesh) {
                for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi)
                    processPrimitive(node->mesh->primitives[pi], world);
            }
            for (cgltf_size ci = 0; ci < node->children_count; ++ci)
                visit(node->children[ci], world);
        };

    // Start from the scene's root nodes (fall back to all nodes if no scene).
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
            visit(data->scene->nodes[i], glm::mat4(1.0f));
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
            if (!data->nodes[i].parent) visit(&data->nodes[i], glm::mat4(1.0f));
    }

    std::cout << "Model: loaded " << path << " -> " << m_meshes.size()
              << " mesh(es), " << texCache.size() << " texture(s)" << std::endl;
    cgltf_free(data);
}
