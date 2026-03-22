#include "asset_manager.hpp"
#include <eng/engine.hpp>
#include <ranges>

namespace eng
{

void AssetManager::init()
{
    static constexpr int MAX_UP = 5;
    std::filesystem::path cwd = "./";
    for(int i = 0; i < MAX_UP; ++i)
    {
        const auto found_dir =
            std::ranges::any_of(std::filesystem::directory_iterator{ cwd }, [](const std::filesystem::directory_entry& e) {
                return e.exists() && e.is_directory() && e.path().string().ends_with("assets");
            });
        if(found_dir)
        {
            assets_dir_path = cwd;
            break;
        }
        else { cwd += "../"; }
    }
    if(assets_dir_path.empty())
    {
        ENG_WARN("Could not find correct directory with eng/ and assets/ dirs in it.");
        return;
    }
}

fs::Path AssetManager::make_path(const fs::Path& path)
{
    if(path.string().starts_with("/")) { return assets_dir_path / fs::Path{ path.string().erase(0, 1) }; }
    return path;
}

fs::FilePtr AssetManager::get_asset(const fs::Path& path, fs::OpenMode mode)
{
    const auto _path = make_path(path);
    return get_engine().fs->open_file(_path, mode);
}

} // namespace eng