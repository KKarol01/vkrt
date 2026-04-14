#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <string_view>

#include <eng/common/hash.hpp>
#include <eng/common/handle.hpp>

namespace eng
{
namespace fs
{

using Path = ::std::filesystem::path;
using PathHash = uint64_t;

enum class OpenMode
{
    RB,
    WB,
    RWB,
};

class FileSystem;

class File
{
  public:
    File() = default;
    File(FileSystem* fs, void* file, size_t size, const Path& path) : fs(fs), file(file), size(size), path(path) {}
    ~File() noexcept;
    void close();
    size_t read(std::byte* out_bytes, size_t bytes, size_t offset);
    std::string read(size_t bytes = ~0ull, size_t offset = 0);
    void write(const std::byte* bytes, size_t size, size_t offset);
    uint64_t get_hash();

    FileSystem* fs{};
    void* file{};
    size_t size{};
    Path path{};

  private:
    uint64_t content_hash{};
};

using FilePtr = std::shared_ptr<File>;

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

} // namespace fs

ENG_DEFINE_HANDLE_STORAGE(fs::DirectoryListener, uintptr_t);
ENG_DEFINE_HANDLE_ALL_GETTERS(fs::DirectoryListener, { return reinterpret_cast<fs::DirectoryListener*>(*handle); });

namespace fs
{

class FileSystem
{
  public:
    bool init();

    FilePtr open_file(const Path& path, OpenMode mode);
    void delete_file(const Path& path);

    Handle<DirectoryListener> make_listener();
    void listen_for_path(const Path& virtual_path, Handle<DirectoryListener> listener);

    Path make_rel_path(const Path& path);
    FilePtr get_asset(const Path& path, OpenMode mode);
    Path get_assets_path() const { return root_dir_path; }

    Path root_dir_path;
    std::map<std::pair<PathHash, OpenMode>, std::weak_ptr<File>> filemap;
    std::deque<DirectoryListener> dir_listeners;
};

} // namespace fs
} // namespace eng