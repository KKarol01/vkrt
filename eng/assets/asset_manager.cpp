#include "asset_manager.hpp"
#include <ranges>
#include <eng/engine.hpp>
#include <eng/assets/loaders.hpp>
#include <eng/assets/serialization.hpp>
#include <eng/assets/compression.hpp>

namespace eng
{

using namespace serialization;

namespace assets
{

Asset s_null_asset{};

void AssetManager::init()
{
    std::vector<fs::Path> engb_paths;
    for(auto it : std::filesystem::directory_iterator{ get_engine().fs->make_rel_path("/assets") })
    {
        if(it.path().extension() == ".engb")
        {
            auto itpath = it.path();
            itpath.make_preferred();
            engb_paths.push_back(itpath);
        }
    }
    std::sort(engb_paths.begin(), engb_paths.end(), [](const auto& a, const auto& b) { return b < a; });
    m_engb_containers_vec.reserve(engb_paths.size());
    for(const auto& p : engb_paths)
    {
        m_engb_containers_vec.emplace_back(get_engine().fs->open_file(p, fs::OpenMode::READ_WRITE_BYTES));
    }
    if(m_engb_containers_vec.empty())
    {
        m_engb_containers_vec.emplace_back(get_engine().fs->get_asset("/assets/assets0.engb", fs::OpenMode::READ_WRITE_CREATE_BYTES));
    }
}

const Asset& AssetManager::get_asset(const fs::Path& file_path)
{
    if(auto it = m_loaded_assets_map.find(file_path); it != m_loaded_assets_map.end()) { return it->second; }

    const auto ext = file_path.extension();

    if(auto assetopt = try_deserialize_asset(file_path); assetopt)
    {
        auto it = m_loaded_assets_map.emplace(file_path, std::move(*assetopt));
        return it.first->second;
    }

    if(ext == ".glb" || ext == ".gltf")
    {
        auto asset = AssetLoaderGLTF::load_from_file(get_engine().fs->make_rel_path(file_path), ImportSettings::KEEP_DATA_BIT);
        if(!asset)
        {
            ENG_WARN("Couldn't load asset {}", file_path.string());
            return s_null_asset;
        }
        asset->path = file_path;
        auto it = m_loaded_assets_map.emplace(file_path, std::move(*asset));

        std::thread serialize_thread{ &AssetManager::try_serialize_asset, this, std::ref(it.first->second) };
        serialize_thread.detach();

        return it.first->second;
    }

    ENG_ERROR("Extension not supported {}", file_path.string());
    return s_null_asset;
}

std::optional<serialization::engb::List> AssetManager::try_find_list_by_hash(uint64_t hash, serialization::engb::Container** out_container)
{
    std::shared_lock lock{ m_engbc_vec_mutex };
    for(auto& c : m_engb_containers_vec)
    {
        const auto list = c.get_asset_list(hash);
        if(list)
        {
            if(out_container) { *out_container = &c; }
            return list;
        }
    }
    return std::nullopt;
}

std::optional<Asset> AssetManager::try_deserialize_asset(const fs::Path& file_path)
{
    serialization::engb::Container* container{};
    const auto listopt = try_find_list_by_hash(ENG_HASH(file_path.string()), &container);
    if(!listopt) { return std::nullopt; }
    ENG_ASSERT(container);
    const auto& list = *listopt;

    if(list.version != 0) { ENG_WARN("Asset {} has invalid version {}", file_path.string(), list.version); }

    ENG_TIMER_SCOPED("Deserializing {}", file_path.string());
    std::shared_lock lock{ m_engbc_vec_mutex };

    std::vector<std::byte> decompressed_asset;
    std::vector<std::byte> file_buf(compression::ZLIB_SCRATCH_SIZE);
    size_t asset_read_offset = 0;
    uint64_t uncompressed_size = 0;
    container->m_file->read((std::byte*)&uncompressed_size, 8, list.asset_start - 8);
    decompressed_asset.reserve(uncompressed_size);
    auto success = compression::zlib_inflate(
        [&](size_t size) {
            const auto file_read = container->get_asset_data(list, std::span{ file_buf.data(), size }, asset_read_offset);
            asset_read_offset += file_read;
            const auto bytes_to_process = std::min(file_read, size);
            auto span = std::span<const std::byte>{ file_buf.data(), bytes_to_process };
            return span;
        },
        [&](std::span<const std::byte> data) {
            decompressed_asset.insert(decompressed_asset.end(), data.begin(), data.end());
        });
    if(!success)
    {
        ENG_WARN("Failed to decompress serialized data {}", file_path.string());
        return std::nullopt;
    }

    size_t read_bytes = 0;
    Asset asset{};
    deserialize(asset, std::span{ decompressed_asset }, read_bytes);
    return asset;
}

void AssetManager::try_serialize_asset(Asset& asset)
{
    if(asset.geometry_data_futures.empty())
    {
        ENG_WARN("Cannot serialize asset without geometries");
        return;
    }

    for(auto& signal : asset.geometry_data_futures)
    {
        signal.wait();
    }

    size_t size = 0;
    ENG_TIMER_SCOPED("Serializing asset {}", asset.path.string());
    std::vector<std::byte> asset_bytes;
    serialize(std::span{ asset_bytes }, asset, size);
    if(size > 0)
    {
        asset_bytes.resize(size);
        size = 0;
        serialize(std::span{ asset_bytes }, asset, size);

        size_t bytes_read = 0;
        size_t bytes_left = asset_bytes.size();

        std::scoped_lock lock{ m_engbc_vec_mutex };
        auto& engbc = get_latest_container();
        engbc.add_asset(0, ENG_HASH(asset.path.string()), engb::ListFlags::CONTENT_COMPRESSED_BIT, {},
                        engb::AssetMetadata{ .uncompressed_size = asset_bytes.size() });

        size_t bytes_compressed = 0;
        const auto res = compression::zlib_deflate(
            [&](size_t size) {
                const auto bytes_to_process = std::min(bytes_left, size);
                auto span = std::span<const std::byte>{ asset_bytes.data() + bytes_read, bytes_to_process };
                bytes_read += bytes_to_process;
                bytes_left -= bytes_to_process;
                return span;
            },
            [&](std::span<const std::byte> data) {
                bytes_compressed += data.size();
                engbc.append_asset_bytes(data, false);
            });

        ENG_ASSERT(res, "Zlib compression failed {}", asset.path.string());
        engbc.append_asset_bytes({}, true);
    }

    asset.geometry_data.resize(0);
    asset.geometry_data_futures.resize(0);
    asset.image_data.resize(0);

    ENG_LOG("Serializing asset {} finished. Written {} bytes.", asset.path.string(), size);
}

} // namespace assets
} // namespace eng