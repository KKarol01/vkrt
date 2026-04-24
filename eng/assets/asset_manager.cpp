#include "asset_manager.hpp"
#include <ranges>
#include <eng/engine.hpp>
#include <eng/assets/loaders.hpp>
#include <eng/assets/serialization.hpp>
#include <eng/assets/compression.hpp>

namespace eng
{
namespace assets
{

Asset null_asset{};

const Asset& AssetManager::get_asset(const fs::Path& file_path)
{
    if(auto it = loaded_assets_map.find(file_path); it != loaded_assets_map.end()) { return it->second; }

    const auto ext = file_path.extension();

    const auto try_deserializing = [this, &file_path]() -> Asset* {
        auto engb_path = file_path;
        engb_path.replace_extension(".engb");
        auto engb_file = get_engine().fs->get_asset(engb_path, fs::OpenMode::RB);
        if(!engb_file) { return nullptr; }
        ENG_TIMER_SCOPED("Deserializing {}", engb_path.string());
        Asset asset{};
        std::vector<std::byte> decompressed_asset;
        bool failure = false;
        unsigned char file_buf[compression::ZLIB_SCRATCH_SIZE];
        auto success = compression::zlib_inflate(
            [&](size_t size) {
                if(failure) { return std::span<const std::byte>{}; }
                const auto file_read = engb_file->read((std::byte*)file_buf, size);
                const auto bytes_to_process = std::min(file_read, size);
                auto span = std::as_bytes(std::span{ file_buf, bytes_to_process });
                return span;
            },
            [&](std::span<const std::byte> data) {
                if(failure) { return; }
                decompressed_asset.insert(decompressed_asset.end(), data.begin(), data.end());
            });
        if(!success)
        {
            ENG_WARN("Failed to decompress serialized data {}", file_path.string());
            return nullptr;
        }

        size_t read_bytes = 0;
        Serializer::deserialize((const std::byte*)decompressed_asset.data(), read_bytes, decompressed_asset.size(), asset);
        auto it = loaded_assets_map.emplace(file_path, std::move(asset));
        return &it.first->second;
    };

    if(auto* deserialized_asset = try_deserializing(); deserialized_asset) { return *deserialized_asset; }

    if(ext == ".glb" || ext == ".gltf")
    {
        auto asset = AssetLoaderGLTF::load_from_file(get_engine().fs->make_rel_path(file_path), ImportSettings::KEEP_DATA_BIT);
        if(!asset)
        {
            ENG_WARN("Couldn't load asset {}", file_path.string());
            return null_asset;
        }
        asset->path = file_path;
        auto it = loaded_assets_map.emplace(file_path, std::move(*asset));

        std::thread serialize_thread{ [&asset = it.first->second] {
            for(auto& signal : asset.geometry_data_futures)
            {
                signal.wait();
            }

            size_t size = 0;
            ENG_TIMER_SCOPED("Serializing asset {}", asset.path.string());
            Serializer::serialize(asset, size);
            if(size > 0)
            {
                std::vector<std::byte> asset_bytes(size);
                size = 0;
                Serializer::serialize(asset, size, asset_bytes.data(), asset_bytes.size());
                auto serialized_path = asset.path;
                serialized_path.replace_extension(".engb");

                auto file = get_engine().fs->get_asset(serialized_path, fs::OpenMode::WB);
                if(!file)
                {
                    ENG_WARN("Could not open file {} for serializing", serialized_path.string());
                    return;
                }
                size_t bytes_read = 0;
                size_t bytes_left = asset_bytes.size();
                bool failure = false;
                compression::zlib_deflate(
                    [&](size_t size) {
                        if(failure) { return std::span<const std::byte>{}; }
                        const auto bytes_to_process = std::min(bytes_left, size);
                        auto span = std::as_bytes(std::span{ asset_bytes.data() + bytes_read, bytes_to_process });
                        bytes_read += bytes_to_process;
                        bytes_left -= bytes_to_process;
                        return span;
                    },
                    [&](std::span<const std::byte> data) {
                        if(failure) { return; }
                        if(file->write(data.data(), data.size()) != data.size())
                        {
                            ENG_WARN("Failed to serialized data to the file {}", serialized_path.string());
                            failure = true;
                        }
                    });
            }

            asset.geometry_data.resize(0);
            asset.geometry_data_futures.resize(0);
            asset.image_data.resize(0);

            ENG_LOG("Serializing asset {} finished. Written {} bytes.", asset.path.string(), size);
        } };
        serialize_thread.detach();

        return it.first->second;
    }

    ENG_ERROR("Extension not supported {}", file_path.string());
    return null_asset;
}

} // namespace assets
} // namespace eng