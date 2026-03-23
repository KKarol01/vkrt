#pragma once

#include <memory>
#include <unordered_map>
#include <eng/fs/fs.hpp>
#include <eng/common/callback.hpp>

namespace eng
{
class AssetManager
{
    struct DirCallback
    {
        Signal<void(const fs::Path&)> on_dir_change;
        std::shared_ptr<void> handle{};
    };

  public:
    void init();
    bool was_properly_init() const { return !assets_dir_path.empty(); }

    fs::Path make_path(const fs::Path& path);
    fs::FilePtr get_asset(const fs::Path& path, fs::OpenMode mode);
    fs::Path get_assets_path() const { return assets_dir_path; }

    void notify_on_dir_change(const fs::Path& virtual_path, const auto& callback)
    {
        if(virtual_path.empty()) { return; }
        if(!virtual_path.string().starts_with('/')) { return; }
        if(virtual_path.has_extension()) { return; }
        auto physical_path = make_path(virtual_path);
        if(!physical_path.is_absolute()) { physical_path = std::filesystem::absolute(physical_path); }
        auto it = dir_change_cb_map.emplace(virtual_path, DirCallback{});
        it.first->second.on_dir_change += callback;
        if(it.second) { install_notify_on_dir_change_callback(virtual_path, physical_path); }
    }

  private:
    void install_notify_on_dir_change_callback(const fs::Path& virtual_path, const fs::Path& physical_path);

    std::unordered_map<fs::Path, DirCallback> dir_change_cb_map;
    fs::Path assets_dir_path;
};
} // namespace eng