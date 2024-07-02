#pragma once

#include <string>

struct ModelTexture {
    ModelTexture(const std::string& name, int w, int h, int ch, uint8_t* data);

    std::string name;
    VkImage image;
    VmaAllocation alloc;
    VmaAllocationInfo alloc_info;
    VkImageView view;
};

struct ModelMaterial {
    std::string name;
    uint32_t base_texture;
};

struct Vertex {
    glm::vec3 pos{};
    glm::vec3 nor{};
    glm::vec2 uv{};
};

struct ModelMesh {
    ModelMaterial material;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct Model {
    void build();

    uint32_t num_vertices, num_indices;
    std::vector<ModelTexture> textures;
    std::vector<ModelMesh> meshes;
};

struct ModelLoader {
    Model load_model(const std::filesystem::path& path);
};
