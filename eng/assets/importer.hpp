#pragma once

#include <filesystem>
#include <vector>
#include <cstdint>
#include <bit>
#include <eng/common/handle.hpp>
#include <eng/common/flags.hpp>
#include <eng/renderer/fwd.hpp>
#include <eng/scene.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

namespace assets {

using asset_index_t = uint16_t;
static constexpr asset_index_t s_max_asset_index = ~asset_index_t{};

enum class AssetFlags : uint32_t {
    INDICES_16_BIT_BIT = 0x1,
};
ENG_ENABLE_FLAGS_OPERATORS(AssetFlags);

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct Range {
    size_t offset{};
    size_t count{};
};

struct Image {
    std::string name;
    uint32_t width{};
    uint32_t height{};
    gfx::ImageFormat format{ gfx::ImageFormat::R8G8B8A8_UNORM };
    std::vector<std::byte> data;
};

struct Texture {
    asset_index_t image{ s_max_asset_index };
    gfx::ImageFilter filtering{ gfx::ImageFilter::LINEAR };
    gfx::ImageAddressing addressing{ gfx::ImageAddressing::REPEAT };
};

struct Material {
    std::string name;
    asset_index_t color_texture{ s_max_asset_index };
    asset_index_t normal_texture{ s_max_asset_index };
};

struct Geometry {
    Range vertex_range{};
    Range index_range{};
};

struct Submesh {
    asset_index_t material{ s_max_asset_index };
    asset_index_t geometry{ s_max_asset_index };
};

struct Mesh {
    std::string name;
    std::vector<asset_index_t> submeshes;
};

struct Node {
    std::string name;
    asset_index_t mesh{ s_max_asset_index };
    std::vector<asset_index_t> nodes;
};

struct Asset {
    bool is_valid() const { return !scene.empty(); }
    Geometry& get_geometry(const Submesh& submesh);
    Node& get_node(asset_index_t idx);
    const Node& get_node(asset_index_t idx) const;
    glm::mat4& get_transform(Node& node);
    const glm::mat4& get_transform(const Node& node) const;
    Mesh& get_mesh(const Node& node);
    Submesh& get_submesh(asset_index_t idx);
    Image& get_image(asset_index_t idx);
    Texture& get_texture(asset_index_t idx);
    Material& get_material(asset_index_t idx);
    asset_index_t make_geometry();
    asset_index_t make_node();
    asset_index_t make_mesh();
    asset_index_t make_submesh();
    asset_index_t make_transform();
    asset_index_t make_texture();
    asset_index_t make_material();

    std::vector<Node> nodes;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Geometry> geometries;
    std::vector<Mesh> meshes;
    std::vector<Submesh> submeshes;
    std::vector<glm::mat4> transforms;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    Flags<AssetFlags> flags;
    std::vector<asset_index_t> scene;
    std::filesystem::path path;
};

struct ImportSettings {};

class Importer {
  public:
    static Asset import_glb(const std::filesystem::path& path, ImportSettings settings = {});

    template <typename Func>
    static inline void dfs_traverse_node_hierarchy(const Asset& asset, const Node& node, Func&& func, const Node* parent = nullptr,
                                                   glm::mat4 parent_transform = glm::identity<glm::mat4>())
        requires std::is_invocable_v<Func, const Node&, const Node*, const glm::mat4&>;
};

} // namespace assets

template <typename Func>
inline void assets::Importer::dfs_traverse_node_hierarchy(const assets::Asset& asset, const assets::Node& node, Func&& func,
                                                          const assets::Node* parent, glm::mat4 parent_transform)
    requires std::is_invocable_v<Func, const Node&, const Node*, const glm::mat4&>
{
    func(node, parent, parent_transform);
    parent_transform = asset.get_transform(node) * parent_transform;
    for(auto i : node.nodes) {
        assets::Importer::dfs_traverse_node_hierarchy(asset, asset.get_node(i), &node, parent_transform);
    }
}