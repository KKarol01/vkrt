#include "asset_manager.hpp"
#include <ranges>
#include <eng/engine.hpp>
#include <eng/assets/loaders.hpp>
#include <eng/assets/serialization.hpp>

namespace eng
{
namespace assets
{

Asset null_asset{};

const Asset& AssetManager::get_asset(const fs::Path& file_path)
{
    if(auto it = loaded_assets_map.find(file_path); it != loaded_assets_map.end()) { return it->second; }

    const auto ext = file_path.extension();

    {
        auto engb_path = file_path;
        engb_path.replace_extension(".engb");
        auto engb_file = get_engine().fs->get_asset(engb_path, fs::OpenMode::RB);
        if(engb_file)
        {
            ENG_TIMER_START("Engb deserialization");
            Asset asset{};

            ENG_TIMER_START("file reading");
            auto bytes = engb_file->read();
            ENG_TIMER_END();
            size_t bytes_read = 0;
            {
                ENG_TIMER_START("desrializing");
                {
                    ENG_TIMER_START("desrializing_1");
                    {
                        ENG_TIMER_START("desrializing_1_1");
                        ENG_TIMER_END();
                    }
                    {
                        ENG_TIMER_START("desrializing_1_2");
                        ENG_TIMER_END();
                    }
                    ENG_TIMER_END();
                }
                {
                    ENG_TIMER_START("reading");
                    Serializer::deserialize((const std::byte*)bytes.data(), bytes_read, bytes.size(), asset);
                    ENG_TIMER_END();
                }
                ENG_TIMER_END();
            }
            auto it = loaded_assets_map.emplace(file_path, std::move(asset));
            ENG_TIMER_END();
            return it.first->second;
        }
    }

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
            ENG_LOG("Serializing asset {}", asset.path.string());
            for(auto& signal : asset.geometry_data_futures)
            {
                signal.wait();
            }

            size_t size = 0;
            Serializer::serialize(asset, size);
            if(size > 0)
            {
                std::vector<std::byte> asset_bytes(size);
                size = 0;
                Serializer::serialize(asset, size, asset_bytes.data(), asset_bytes.size());
                auto serialized_path = asset.path;
                serialized_path.replace_extension(".engb");
                auto file = get_engine().fs->get_asset(serialized_path, fs::OpenMode::WB);
                file->write(asset_bytes.data(), asset_bytes.size(), 0);
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