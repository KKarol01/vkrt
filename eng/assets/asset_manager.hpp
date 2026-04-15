#pragma once

#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/ecs/components.hpp>
#include <eng/fs/fs.hpp>

namespace eng
{
namespace assets
{

struct Asset
{
    struct Node
    {
        std::string name;
        Range32u meshes{};
        uint32_t transform{ ~0u };
        uint32_t parent{ ~0u };
        Range32u children{};
    };
	fs::Path path;
    std::vector<Handle<gfx::Image>> images;
    std::vector<gfx::ImageView> textures;
    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<Handle<gfx::Mesh>> meshes;
    std::vector<ecs::Transform> transforms;
    std::vector<Node> nodes;
    std::vector<uint32_t> root_nodes;
};

class AssetManager
{
  public:
    const Asset& get_asset(const fs::Path& file_path);

  private:
    std::unordered_map<fs::Path, Asset> loaded_assets_map;
};

} // namespace assets
} // namespace eng