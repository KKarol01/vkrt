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
#include <eng/common/handle_map.hpp>
#include <eng/common/types.hpp>

namespace scene
{

struct Image
{
    std::string name;
    Handle<gfx::Image> gfx_handle;
    gfx::ImageFormat format;
    std::vector<std::byte> data;
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{ 1 };
};

struct Texture
{
    Handle<Image> image;
    Handle<gfx::Texture> gfx_handle;
    gfx::ImageFiltering filtering{ gfx::ImageFiltering::LINEAR };
    gfx::ImageAddressing addressing{ gfx::ImageAddressing::REPEAT };
};

struct Material
{
    std::string name;
    Handle<gfx::Material> gfx_handle;
    Handle<Texture> base_color_texture;
};

struct Geometry
{
    Handle<gfx::Geometry> gfx_handle;
    std::vector<gfx::Vertex> vertices;
    std::vector<gfx::Index> indices;
    std::vector<gfx::Meshlet> meshlets;
};

struct Submesh
{
    Handle<gfx::Mesh> gfx_handle;
    Handle<Geometry> geometry;
    Handle<Material> material;
};

struct Mesh
{
    std::string name;
    std::vector<Submesh> submeshes;
};

struct Node
{
    std::string name;
    std::vector<Handle<Node>> children;
    glm::mat4 transform{ 1.0f };
    glm::mat4 final_transform{ 1.0f };
    Handle<Mesh> mesh;
};

struct Model
{
    std::filesystem::path path;
    Handle<Node> root_node;
};

struct NodeInstance
{
    std::string name;
    std::vector<Handle<NodeInstance>> children;
    glm::mat4 transform{ 1.0f };
    ecs::Entity entity;
};

struct ModelInstance
{
    Handle<NodeInstance> root_node;
};

class Scene
{
  public:
    Handle<Model> load_from_file(const std::filesystem::path& path);
    // Handle<Node> load_from_asset(assets::Asset& asset);
    Handle<ModelInstance> instance_model(Handle<Model> node);
    // glm::mat4 get_final_transform(Handle<Entity> handle) const { return instance_handles.at(handle)->final_transform; }

    void update_transform(Handle<NodeInstance> entity, glm::mat4 transform);
    // void _update_transform(uint32_t idx, glm::mat4 t = { 1.0f });

  private:
    void upload_model_data(Handle<Model> model);

    void traverse_dfs(Handle<Node> node, const auto& func);
    void traverse_dfs(Node& node, const auto& func)
        requires std::invocable<decltype(func), scene::Node&>;

    // TODO: maybe make this private too (used in many places -- possibly bad interface)
  public:
    float debug_dir_light_dir[3]{ -0.45453298, -0.76604474, 0.45450562 };
    float debug_dir_light_pos[3]{ 11.323357, 12.049147, -10.638633 };

    HandleMap<Image> images;
    HandleMap<Texture> textures;
    HandleMap<Material> materials;
    HandleMap<Geometry> geometries;
    HandleMap<Mesh> meshes;
    HandleMap<Node> nodes;
    HandleMap<Model> models;
    HandleMap<NodeInstance> node_instances;
    std::vector<ModelInstance> scene;
};
} // namespace scene

// void scene::Scene::traverse_dfs(Handle<scene::Node> node, const auto& func) {
//     auto& n = *node_handles.at(node);
//     traverse_dfs(n, func);
// }
//
// void scene::Scene::traverse_dfs(scene::Node& node, const auto& func)
//     requires std::invocable<decltype(func), scene::Node&>
//{
//     func(node);
//     for(auto& c : node.children) {
//         traverse_dfs(*node_handles.at(c), func);
//     }
// }