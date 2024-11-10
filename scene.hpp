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
    Handle<Entity> handle{};
    uint32_t components{};
    uint32_t parent{ ~0u };
    uint32_t children_offset{};
    uint32_t children_count{};
};

struct MaterialAsset {
    std::string name;
    Handle<RenderMaterial> material_handle;
    Handle<Image> color_texture_handle;
};

struct MeshAsset {
    std::string name;
    Handle<RenderMesh> rm_handle;
    MaterialAsset* material;
    BoundingBox aabb;
};

struct ModelAsset {
    std::filesystem::path path;
    Handle<RenderGeometry> geometry;
    std::vector<MeshAsset> meshes;
    std::vector<MaterialAsset> materials;
    std::vector<Handle<Image>> textures;
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
    void _update_transform(uint32_t idx, glm::mat4 t = { 1.0f });

  public:
    std::vector<Node> nodes;
    std::vector<uint32_t> root_nodes;
    std::vector<glm::mat4> final_transforms;
    std::deque<ModelAsset> model_assets;
    std::unordered_map<std::filesystem::path, Handle<ModelAsset>> path_model_assets;
    std::unordered_map<Handle<ModelAsset>, ModelAsset*> handle_model_assets;
    std::unordered_map<Handle<Entity>, uint32_t> entity_node_idxs;
};
