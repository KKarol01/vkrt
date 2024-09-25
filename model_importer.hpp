#pragma once

#include <string>
#include <utility>
#include <optional>
#include <filesystem>
#include <glm/glm.hpp>

struct ImportedModel {
    struct Vertex {
        glm::vec3 pos{};
        glm::vec3 nor{};
        glm::vec2 uv{};
    };

    struct Texture {
        std::string name;
        std::pair<uint32_t, uint32_t> size;
        std::vector<std::byte> rgba_data;
    };

    struct Material {
        std::string name;
        std::optional<uint32_t> color_texture;
        std::optional<uint32_t> normal_texture;
    };

    struct Mesh {
        std::string name;
        uint32_t vertex_offset{ 0 };
        uint32_t index_offset{ 0 };
        uint32_t vertex_count{ 0 };
        uint32_t index_count{ 0 };
        std::optional<uint32_t> material;
    };

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
};

class ModelImporter {
  public:
    static ImportedModel import_model(const std::filesystem::path& path);
};
