#pragma once

#include <filesystem>
#include <deque>
#include <unordered_map>
#include "handle.hpp"
#include "handle_vec.hpp"
#include "renderer.hpp"

class Scene {
  public:
    struct ModelAsset {
        struct Mesh {
            std::string name;
            Handle<MeshBatch> mesh_handle;
            uint32_t material;
        };
        struct Material {
            std::string name;
            Handle<MaterialBatch> material_handle;
            Handle<TextureBatch> color_texture_handle;
        };

        std::string name;
        std::filesystem::path path;
        Handle<GeometryBatch> geometry;
        std::vector<Mesh> meshes;
        std::vector<Material> materials;
        std::vector<Handle<TextureBatch>> textures;
    };

    struct MeshInstance {
        ModelAsset::Mesh* mesh{};
        Handle<::MeshInstance> renderer_handle;
    };

    struct ModelInstance {
        Handle<ModelInstance> handle;
        ModelAsset* model{};
        size_t instance_offset{};
        size_t instance_count{};
    };

    Handle<ModelAsset> load_from_file(const std::filesystem::path& path);
    Handle<ModelInstance> instance_model(Handle<ModelAsset> asset, InstanceSettings settings);

    std::deque<ModelAsset> model_assets;
    std::unordered_map<Handle<ModelAsset>, ModelAsset*> model_asset_handles;
    std::vector<ModelInstance> model_instances;
    std::vector<MeshInstance> mesh_instances;
    std::vector<glm::mat4x3> transforms;
};