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

// Open mode. _BYTES suffix means posix 'b' flag that disables special '\n' handling on windows.
enum class OpenMode
{
    NONE,
    READ_BYTES,              // read from start, fail on no file
    WRITE_CREATE_BYTES,      // write from start, always destroy contents, create new on no file
    READ_WRITE_BYTES,        // read/write from start, error on no file
    READ_WRITE_CREATE_BYTES, // read/write from start, destroy contents, create new on no file
    LAST_ENUM,
};

inline bool open_mode_is_read(OpenMode mode);
inline bool open_mode_is_write(OpenMode mode);

class FileSystem;

class File
{
  public:
    File() = default;
    File(FileSystem* fs, const Path& path, OpenMode mode);
    ~File() noexcept;
    File(File&& o) noexcept { *this = std::move(o); }
    File& operator=(File&& o) noexcept
    {
        fs = std::exchange(o.fs, nullptr);
        file = std::move(o.file);
        size = std::exchange(o.size, 0ull);
        path = std::move(o.path);
        mode = std::exchange(o.mode, OpenMode::NONE);
        return *this;
    }
    bool is_open() const;
    void close();
    size_t read(std::byte* out_bytes, size_t bytes, size_t offset = ~0ull);
    std::string read(size_t bytes = ~0ull, size_t offset = ~0ull);
    size_t write(const std::byte* bytes, size_t size, size_t offset = ~0ull);
    void delete_from_disk();
    bool eof() const;
    uint64_t get_hash();

    FileSystem* fs{};
    std::fstream file;
    size_t size{};
    Path path{};
    OpenMode mode{ OpenMode::NONE };

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