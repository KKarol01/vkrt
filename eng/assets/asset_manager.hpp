#pragma once

#include <deque>
#include <mutex>
#include <shared_mutex>

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

struct Node
{
    std::string name;
    Range32u meshes{};
    u32 transform{ ~0u };
    u32 parent{ ~0u };
    Range32u children{};
};

struct Asset
{
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
    std::vector<ecsc::Transform> transforms;
    std::vector<Node> nodes;
    std::vector<u32> root_nodes;

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
    serialization::engb::Container& get_latest_container();
    std::optional<serialization::engb::List> try_find_list_by_hash(u64 hash,
                                                                   serialization::engb::Container** out_container = nullptr);

    std::optional<Asset> try_deserialize_asset(const fs::Path& file_path);
    void serialize_asset_to_enbc_thread(Asset& asset);

    std::unordered_map<fs::Path, Asset> m_loaded_assets_map;
    std::vector<serialization::engb::Container> m_engb_containers_vec; // assetN, assetN-1, assetN-2; from newest to oldest
    std::shared_mutex m_engbc_vec_mutex;
    std::vector<std::unique_ptr<std::jthread>> m_serializing_threads;
};

} // namespace assets

namespace serialization
{
ENG_SERIALIZATION_DECLARE_CUSTOM_FUNCTIONS(assets::Asset);
template <> inline constexpr auto get_struct_fields<assets::Node>()
{
    return std::make_tuple(ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(assets::Node, name),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(assets::Node, meshes),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(assets::Node, transform),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(assets::Node, parent),
                           ENG_SERIALIZATION_DECLARE_STRUCT_FIELD(assets::Node, children));
}
} // namespace serialization
} // namespace eng