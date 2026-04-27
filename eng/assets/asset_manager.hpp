#pragma once

#include <deque>

#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/ecs/components.hpp>
#include <eng/fs/fs.hpp>
#include <eng/assets/serialization.hpp>

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

    Asset() noexcept = default;
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    Asset(Asset&&) noexcept = default;
    Asset& operator=(Asset&&) noexcept = default;

    fs::Path path;
    std::vector<Handle<gfx::Image>> images;
    std::vector<gfx::ImageView> textures;
    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<Handle<gfx::Mesh>> meshes;
    std::vector<ecs::Transform> transforms;
    std::vector<Node> nodes;
    std::vector<uint32_t> root_nodes;

    std::deque<gfx::ParsedGeometryReadySignal> geometry_data;
    std::deque<std::shared_future<gfx::ParsedGeometryData>> geometry_data_futures;
    std::vector<gfx::ParsedImageData> image_data;
};

class AssetManager
{
  public:
    void init();
    const Asset& get_asset(const fs::Path& file_path);

  private:
    engb::Container& get_latest_container() { return m_engb_containers_vec.front(); }

    std::unordered_map<fs::Path, Asset> m_loaded_assets_map;
    std::vector<engb::Container> m_engb_containers_vec; // assetN, assetN-1, assetN-2; from newest to oldest
};

} // namespace assets
} // namespace eng