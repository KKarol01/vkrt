#pragma once
#include <filesystem>
#include <deque>
#include <unordered_map>
#include "common/types.hpp"
#include "common/spatial.hpp"
#include "common/components.hpp"
#include "handle.hpp"
#include "renderer.hpp"
#include "ecs.hpp"

struct Node {
    template <typename Comp> bool has_component() const {
        return components & (1u << EntityComponentIdGenerator<>::get_id<Comp>());
    };

    std::string name;
    Handle<Entity> handle;
    u32 components{};
    u32 parent{ ~0u };
    u32 children_offset{};
    u32 children_count{};
};

struct Material {
    std::string name;
    Handle<MaterialBatch> material_handle;
    Handle<RenderTexture> color_texture_handle;
};

struct Mesh {
    std::string name;
    Handle<RenderMesh> mesh_handle;
    Material* material;
    BoundingBox aabb;
};

struct ModelAsset {
    std::filesystem::path path;
    Handle<RenderGeometry> geometry;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Handle<RenderTexture>> textures;
};

class Scene {
  public:
    Handle<ModelAsset> load_from_file(const std::filesystem::path& path);
    Handle<Node> instance_model(Handle<ModelAsset> asset, InstanceSettings settings);
    template <typename Comp> Comp& attach_component(Node* node, Comp&& comp);
    glm::mat4 get_final_transform(Handle<Entity> handle) const {
        return final_transforms.at(entity_node_idxs.at(handle));
    }
    Node& get_node(Handle<Entity> handle) { return nodes.at(entity_node_idxs.at(handle)); }

    void update_transform(Handle<Entity> entity);
    void _update_transform(u32 idx, glm::mat4 t = { 1.0f });

  public:
    std::vector<Node> nodes;
    std::vector<u32> root_nodes;
    std::vector<glm::mat4> final_transforms;
    std::deque<ModelAsset> model_assets;
    std::unordered_map<std::filesystem::path, Handle<ModelAsset>> path_model_assets;
    std::unordered_map<Handle<ModelAsset>, ModelAsset*> model_asset_handles;
    std::unordered_map<Handle<Entity>, u32> entity_node_idxs;
};
