#pragma once

#include <memory>
#include <unordered_map>
#include <deque>
#include <eng/common/handle.hpp>
#include <eng/fs/fs.hpp>

namespace eng
{
namespace assets
{

class DirectoryListener
{
  public:
    void add_paths(std::span<const fs::Path> paths)
    {
        std::scoped_lock lock{ mutex };
        changed_files.insert(paths.begin(), paths.end());
    }

    void consume_paths(auto&& cb)
    {
        std::scoped_lock lock{ mutex };
        if(changed_files.empty()) { return; }

        auto vec = std::vector<fs::Path>(std::make_move_iterator(changed_files.begin()),
                                         std::make_move_iterator(changed_files.end()));
        cb(std::move(vec));
        changed_files.clear();
    }

    std::unordered_set<fs::Path> changed_files;
    std::unordered_map<fs::Path, std::shared_ptr<void>> listeners;
    std::mutex mutex;
};

} // namespace assets

ENG_DEFINE_HANDLE_STORAGE(assets::DirectoryListener, uintptr_t);
ENG_DEFINE_HANDLE_ALL_GETTERS(assets::DirectoryListener,
                              { return handle ? reinterpret_cast<assets::DirectoryListener*>(*handle) : nullptr; });

namespace assets
{
class AssetManager
{
  public:
    void init();
    bool was_properly_init() const { return !assets_dir_path.empty(); }

    fs::Path make_path(const fs::Path& path);
    fs::FilePtr get_asset(const fs::Path& path, fs::OpenMode mode);
    fs::Path get_assets_path() const { return assets_dir_path; }

    Handle<DirectoryListener> make_listener()
    {
        auto* ptr = &dir_listeners.emplace_back();
        return Handle<DirectoryListener>{ reinterpret_cast<uintptr_t>(ptr) };
    }

    void listen_for_path(const fs::Path& virtual_path, Handle<DirectoryListener> listener);

  private:
    std::deque<DirectoryListener> dir_listeners;
    fs::Path assets_dir_path;
};

} // namespace assets
} // namespace eng