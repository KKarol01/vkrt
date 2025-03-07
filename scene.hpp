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
#include "ecs.hpp"
#include "renderer.hpp"

namespace scene {

struct Primitive {
    Handle<RenderGeometry> geometry_handle{};
    Handle<RenderMesh> mesh_handle{};
    Handle<RenderBLAS> blas_handle{};
    Handle<RenderMaterial> material_handle{};
};

struct Node {
    std::string name;
    std::vector<Node*> children;
    // rendering primitives (triangles with material)
    std::vector<Primitive> primitives;
    glm::mat4 transform{ 1.0f };
    glm::mat4 final_transform{ 1.0f };
    Handle<Node> handle;
};

struct NodeInstance {
    bool has_children() const;

    std::string name;
    // children are same size as node's children array, except some pointers can be nullptrs
    std::vector<NodeInstance*> children;
    // actually rendered primitives (may not all be present as in referenced node_hadle)
    std::vector<components::Entity> primitives;
    glm::mat4 transform{ 1.0f };
    glm::mat4 final_transform{ 1.0f };
    Handle<NodeInstance> instance_handle;
    Handle<Node> node_handle;
};

template <typename Func>
    requires std::is_invocable_v<Func, Node*, uint32_t>
void traverse_node_hierarchy_indexed(Node* node, Func f) {
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

template <typename Func>
    requires std::is_invocable_v<Func, NodeInstance*, uint32_t>
void traverse_node_hierarchy_indexed(NodeInstance* node, Func f) {
    std::stack<NodeInstance*> stack;
    stack.push(node);
    uint32_t index = 0;
    while(!stack.empty()) {
        auto* n = stack.top();
        stack.pop();
        f(n, index++);
        for(auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
            if(*it) { stack.push(*it); }
        }
    }
}

class Scene {
  public:
    Handle<Node> load_from_file(const std::filesystem::path& path);
    Handle<NodeInstance> instance_model(Handle<Node> asset);
    // glm::mat4 get_final_transform(Handle<Entity> handle) const { return instance_handles.at(handle)->final_transform; }

    void update_transform(Handle<scene::NodeInstance> entity, glm::mat4 transform);
    // void _update_transform(uint32_t idx, glm::mat4 t = { 1.0f });

    Node* add_node();
    NodeInstance* add_instance();
    NodeInstance& get_instance(Handle<scene::NodeInstance> entity) { return *instance_handles.at(entity); }

    // TODO: maybe make this private too (used in many places -- possibly bad interface)
  public:
    float debug_dir_light_dir[3]{ 0.1f, -0.8f, 0.0f };

    // Storage for all nodes so they can reference one another.
    std::deque<Node> nodes;
    // Instances of particular node
    std::deque<NodeInstance> node_instances;
    // Actual actors present on the scene
    std::vector<NodeInstance*> scene;
    std::unordered_map<Handle<Node>, Node*> node_handles;
    std::unordered_map<Handle<NodeInstance>, NodeInstance*> instance_handles;
};
} // namespace scene
