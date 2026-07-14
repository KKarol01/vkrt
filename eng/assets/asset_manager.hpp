#pragma once

#include <deque>
#include <mutex>
#include <shared_mutex>
#include <future>

#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <eng/ecs/components.hpp>
#include <eng/fs/fs.hpp>
#include <eng/assets/serialization.hpp>

namespace eng
{
namespace assets
{

struct Node
{
    static constexpr auto get_struct_fields()
    {
        return std::make_tuple(serialization::StructField{ &Node::name }, serialization::StructField{ &Node::meshes },
                               serialization::StructField{ &Node::transform }, serialization::StructField{ &Node::parent },
                               serialization::StructField{ &Node::children });
    }
    std::string name;
    Range32u meshes{};
    u32 transform{ ~0u };
    u32 parent{ ~0u };
    Range32u children{};
};

struct ParsedGeometryData
{
    static constexpr auto get_struct_fields()
    {
        return std::make_tuple(serialization::StructField{ &ParsedGeometryData::vertex_layout },
                               serialization::StructField{ &ParsedGeometryData::positions },
                               serialization::StructField{ &ParsedGeometryData::attributes },
                               serialization::StructField{ &ParsedGeometryData::indices },
                               serialization::StructField{ &ParsedGeometryData::meshlets });
    }
    Flags<gfx::VertexComponent> vertex_layout;
    std::vector<float> positions{};
    std::vector<float> attributes{};
    std::vector<u16> indices{};
    std::vector<gfx::Meshlet> meshlets{};
};
using ParsedGeometryReadySignal = std::promise<ParsedGeometryData>;

struct ParsedImageData
{
    static constexpr auto get_struct_fields()
    {
        return std::make_tuple(serialization::StructField{ &ParsedImageData::name },
                               serialization::StructField{ &ParsedImageData::width },
                               serialization::StructField{ &ParsedImageData::height },
                               serialization::StructField{ &ParsedImageData::format },
                               serialization::StructField{ &ParsedImageData::data });
    }
    StackString<128> name;
    u32 width{};
    u32 height{};
    gfx::ImageFormat format{};
    std::vector<std::byte> data;
};

struct Asset
{
    Asset() noexcept = default;
    Asset(const Asset&) = delete;
    Asset& operator=(const Asset&) = delete;
    Asset(Asset&&) noexcept = default;
    Asset& operator=(Asset&&) noexcept = default;

    void serialize(serialization::Context& ctx) const;
    void deserialize(serialization::Context& ctx);

    fs::Path path;
    std::vector<Handle<gfx::Image>> images;
    std::vector<gfx::ImageView> textures;
    std::vector<Handle<gfx::Geometry>> geometries;
    std::vector<Handle<gfx::Material>> materials;
    std::vector<Handle<gfx::Mesh>> meshes;
    std::vector<ecsc::Transform> transforms;
    std::vector<Node> nodes;
    std::vector<u32> root_nodes;

    std::deque<assets::ParsedGeometryReadySignal> geometry_data;
    std::deque<std::shared_future<assets::ParsedGeometryData>> geometry_data_futures;
    std::vector<assets::ParsedImageData> image_data;
};

class AssetManager
{
  public:
    void init();
    const Asset& get_asset(const fs::Path& file_path);

  private:
    serialization::engb::Container& get_latest_container();
    std::optional<serialization::engb::List> try_find_list_by_hash(u64 hash, serialization::engb::Container** out_container = nullptr);

    std::optional<Asset> try_deserialize_asset(const fs::Path& file_path);
    void serialize_asset_to_enbc_thread(Asset& asset);

    std::unordered_map<fs::Path, Asset> m_loaded_assets_map;
    std::vector<serialization::engb::Container> m_engb_containers_vec; // assetN, assetN-1, assetN-2; from newest to oldest
    std::shared_mutex m_engbc_vec_mutex;
    std::vector<std::unique_ptr<std::jthread>> m_serializing_threads;
};

} // namespace assets
} // namespace eng