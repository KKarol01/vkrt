#pragma once

#include <filesystem>
#include <map>
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

    struct ModelInstance {
        Handle<ModelAsset> asset;
        std::vector<Handle<MeshInstance>> mesh_instance_handles;
    };

    Handle<ModelAsset> load_from_file(const std::filesystem::path& path);
    Handle<ModelInstance> instance_model(Handle<ModelAsset> asset, InstanceSettings settings);

    std::unordered_map<Handle<ModelAsset>, ModelAsset> model_assets;
    HandleVector<ModelInstance> model_instances;
};