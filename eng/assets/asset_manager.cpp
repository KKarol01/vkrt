#include "asset_manager.hpp"
#include <ranges>
#include <eng/engine.hpp>
#include <eng/assets/loaders.hpp>

namespace eng
{
namespace assets
{

Asset null_asset{};

const Asset& AssetManager::get_asset(const fs::Path& file_path)
{
    if(auto it = loaded_assets_map.find(file_path); it != loaded_assets_map.end()) { return it->second; }

    const auto ext = file_path.extension();
    if(ext == ".glb" || ext == ".gltf")
    {
        auto asset = AssetLoaderGLTF::load_from_file(get_engine().fs->make_rel_path(file_path));
        if(!asset)
        {
            ENG_WARN("Couldn't load asset {}", file_path.string());
            return null_asset;
        }
        asset->path = file_path;
        auto it = loaded_assets_map.emplace(file_path, std::move(*asset));
        return it.first->second;
    }

    ENG_ERROR("Extension not supported {}", file_path.string());
    return null_asset;
}

} // namespace assets
} // namespace eng