#pragma once
#include <filesystem>
#include <deque>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include "common/types.hpp"
#include "common/spatial.hpp"
#include "common/components.hpp"
#include "handle.hpp"
#include "renderer.hpp"
#include "ecs.hpp"

struct MaterialAsset {
    std::string name;
    Handle<RenderMaterial> material_handle;
    Handle<Image> color_texture_handle;
    Handle<Image> normal_texture_handle;
    Handle<Image> metallic_roughness_texture_handle;
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

namespace scene {

struct Primitive {
    Handle<RenderGeometry> geometry_handle{};
    Handle<RenderMesh> mesh_handle{};
    Handle<RenderBLAS> blas_handle{};
    Handle<RenderMaterial> material_handle{};
};

struct Node {
    template <typename Comp> bool has_component() const {
        return components & (1u << EntityComponentIdGenerator<>::get_id<Comp>());
    };
    std::string name;
    std::vector<Node*> children;
    std::vector<Primitive> primitives;
    uint32_t components{};
    Handle<Entity> handle{};
    glm::mat4 transform{ 1.0f };
    glm::mat4 final_transform{ 1.0f };
};

struct NodeInstance {
    Handle<Entity> handle;
    Handle<Entity> node_handle;
    // children are same size as node's children array, except some pointers can be nullptrs
    std::vector<NodeInstance*> children;
    std::vector<Handle<RenderInstance>> primitive_handles;
    glm::mat4 transform{ 1.0f };
    glm::mat4 final_transform{ 1.0f };
};

template <typename Func>
    requires std::is_invocable_v<Func, Node*, uint32_t>
void traverse_node_hierarchy_indexed(Node* node, Func&& f) {
    std::stack<Node*> stack;
    stack.push(node);
    uint32_t index = 0;
    while(!stack.empty()) {
        Node* n = stack.top();
        stack.pop();
        f(n, index++);
        for(auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
            stack.push(*it);
        }
    }
}

class Scene {
  public:
    Handle<Entity> load_from_file(const std::filesystem::path& path);
    Handle<Entity> instance_model(Handle<Entity> asset);
    template <typename Comp> Comp& attach_component(Node* node, Comp&& comp);
    glm::mat4 get_final_transform(Handle<Entity> handle) const { return instance_handles.at(handle)->final_transform; }
    Node* add_node();
    NodeInstance* add_instance();

    void update_transform(Handle<Entity> entity);
    void _update_transform(uint32_t idx, glm::mat4 t = { 1.0f });

  public:
    std::deque<Node> nodes;
    std::deque<NodeInstance> node_instances;
    std::vector<NodeInstance*> scene;
    std::unordered_map<Handle<Entity>, Node*> node_handles;
    std::unordered_map<Handle<Entity>, NodeInstance*> instance_handles;
};
} // namespace scene
