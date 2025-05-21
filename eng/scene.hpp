#pragma once
#include <filesystem>
#include <deque>
#include <stack>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include "./common/types.hpp"
#include "./common/spatial.hpp"
#include <eng/common/components.hpp>
#include <eng/common/handle.hpp>
#include <eng/ecs/ecs.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/assets/importer.hpp>

namespace assets { // don't know why this is necessary
struct Asset;
}

namespace scene {

struct Mesh {
    std::string name;
    std::vector<Handle<gfx::Mesh>> submeshes;
};

struct Node {
    std::string name;
    std::vector<Handle<Node>> children;
    glm::mat4 transform{ 1.0f };
    std::optional<Mesh> mesh;
};

struct NodeInstance {
    std::string name;
    std::vector<Handle<NodeInstance>> children;
    glm::mat4 transform{ 1.0f };
    ecs::Entity entity;
};

class Scene {
  public:
    Handle<Node> load_from_file(const std::filesystem::path& path);
    Handle<Node> load_from_asset(assets::Asset& asset);
    Handle<NodeInstance> instance_model(Handle<Node> node);
    // glm::mat4 get_final_transform(Handle<Entity> handle) const { return instance_handles.at(handle)->final_transform; }

    void update_transform(Handle<NodeInstance> entity, const glm::mat4& transform = glm::mat4{ 1.0f });
    // void _update_transform(uint32_t idx, glm::mat4 t = { 1.0f });

  private:
    Handle<Node> add_node(Node** out = nullptr);
    Handle<NodeInstance> add_instance(NodeInstance** out = nullptr);
    Node& get_node(Handle<Node> entity) { return *node_handles.at(entity); }
    NodeInstance& get_instance(Handle<NodeInstance> entity) { return *instance_handles.at(entity); }

    void traverse_dfs(Handle<Node> node, const auto& func);
    void traverse_dfs(Node& node, const auto& func)
        requires std::invocable<decltype(func), scene::Node&>;

    // TODO: maybe make this private too (used in many places -- possibly bad interface)
  public:
    float debug_dir_light_dir[3]{ -0.45453298, -0.76604474, 0.45450562 };
    float debug_dir_light_pos[3]{ 11.323357, 12.049147, -10.638633 };

    std::deque<Node> nodes;                  // Storage for all nodes so they can reference one another.
    std::deque<NodeInstance> node_instances; // Instances of particular node
    std::vector<NodeInstance*> scene;        // Actual actors present on the scene
    std::unordered_map<Handle<Node>, Node*> node_handles;
    std::unordered_map<Handle<NodeInstance>, NodeInstance*> instance_handles;
};
} // namespace scene

void scene::Scene::traverse_dfs(Handle<scene::Node> node, const auto& func) {
    auto& n = *node_handles.at(node);
    traverse_dfs(n, func);
}

void scene::Scene::traverse_dfs(scene::Node& node, const auto& func)
    requires std::invocable<decltype(func), scene::Node&>
{
    func(node);
    for(auto& c : node.children) {
        traverse_dfs(*node_handles.at(c), func);
    }
}