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
#include <glm/mat4x3.hpp>

namespace assets {

using asset_index_t = uint16_t;
static constexpr asset_index_t s_max_asset_index = ~asset_idnex_t{};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

enum class AssetFlags : uint32_t {
    INDICES_16_BIT_BIT = 0x1,
};
ENG_ENABLE_FLAGS_OPERATORS(AssetFlags);

struct Image {
    std::string name;
    uint32_t width{};
    uint32_t height{};
    gfx::ImageFormat format{ gfx::ImageFormat::R8G8B8A8_UNORM };
    std::vector<std::byte> data;
};

struct Texture {
    asset_index_t image{ s_max_asset_index };
    gfx::ImageFilter filtering;
    gfx::ImageAddressing addressing;
};

struct Material {
    asset_index_t _texture{ s_max_asset_index };
    asset_index_t color_texture{ s_max_asset_index };
    asset_index_t color_texture{ s_max_asset_index };
};

struct Mesh {};

struct Node {
    std::vector<asset_index_t> meshes;
    std::vector<asset_index_t> nodes;
    asset_index_t transform;
};

struct Asset {
    Flags<AssetFlags> flags;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Image> images;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<glm::mat4x3> transforms;
    Node node;
};

struct ImportSettings {};

class Importer {
  public:
    static import_glb(const std::filesystem::path& path, ImportSettings settings = {});
};

} // namespace assets