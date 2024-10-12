#pragma once

#include <filesystem>
#include <deque>
#include <unordered_map>
#include "handle.hpp"
#include "handle_vec.hpp"
#include "renderer.hpp"
#include "common/types.hpp"
#include "common/spatial.hpp"

class Scene {
  public:
    struct Node {
        Handle<Entity> handle;
        u32 parent;
        Span<u32> children;

    };

    struct ModelAsset {
        struct Mesh {
            std::string name;
            Handle<MeshBatch> mesh_handle;
            uint32_t material;
            BoundingBox aabb;
        };
        struct Material {
            std::string name;
            Handle<MaterialBatch> material_handle;
            Handle<TextureBatch> color_texture_handle;
        };

        std::filesystem::path path;
        Handle<GeometryBatch> geometry;
        std::vector<Mesh> meshes;
        std::vector<Material> materials;
        std::vector<Handle<TextureBatch>> textures;
    };

    struct ModelInstance {
        std::string name;
        size_t instance_offset{};
        size_t instance_count{};
        ModelAsset* model{};
        glm::mat4 transform{ 1.0f };
    };

    struct MeshInstance {
        ModelAsset::Mesh* mesh{};
        Handle<ModelInstance> model_instance;
        Handle<::MeshInstance> renderer_handle;
    };

    Handle<ModelAsset> load_from_file(const std::filesystem::path& path);
    Handle<ModelInstance> instance_model(Handle<ModelAsset> asset, InstanceSettings settings);

    std::vector<Node> nodes;

    std::deque<ModelAsset> model_assets;
    std::unordered_map<Handle<ModelAsset>, ModelAsset*> model_asset_handles;
    HandleVector<ModelInstance> model_instances;
    std::vector<MeshInstance> mesh_instances;
    std::vector<glm::mat4> transforms;
};
