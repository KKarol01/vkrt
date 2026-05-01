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
    bool is_read() const { return open_mode_is_read(mode); }
    bool is_write() const { return open_mode_is_write(mode); }
    void open();
    void close();
    void flush();
    size_t read(std::byte* out_bytes, size_t bytes, size_t offset = ~0ull);
    std::string read(size_t bytes = ~0ull, size_t offset = ~0ull);
    size_t write(const std::byte* bytes, size_t size, size_t offset = ~0ull, bool flush = false);
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
    void push_path(std::span<const fs::Path> paths)
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

    // Open a file with system path (relative or absolute)
    FilePtr open_file(const Path& path, OpenMode mode);
    // Delete file from the disk
    void delete_file(const Path& path);

    // Create recursive listener for file changes
    Handle<DirectoryListener> make_listener();
    // Append a path to listen for file changes into created listener
    void listen_for_path(const Path& virtual_path, Handle<DirectoryListener> listener);

    // From virtual path like '/assets/texture.png' make system relative path like '../../assets/texture.png'
    Path make_rel_path(const Path& virtual_path);
    // Open a file after transforing virtual path
    FilePtr get_asset(const Path& virtual_path, OpenMode mode);
    // Get relative path to the directory where assets are located
    Path get_assets_path() const { return root_dir_path; }

    Path root_dir_path;
    std::map<std::pair<PathHash, OpenMode>, std::weak_ptr<File>> m_files_map;
    std::deque<DirectoryListener> m_dir_listeners_vec;
};

} // namespace fs
} // namespace eng