#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <string_view>

#include <eng/string/stack_string.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/handle.hpp>

namespace eng
{
namespace fs
{

using Path = ::std::filesystem::path;
using PathHash = u64;

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
        m_fs = std::exchange(o.m_fs, nullptr);
        m_file = std::move(o.m_file);
        m_size = std::exchange(o.m_size, 0ull);
        m_path = std::move(o.m_path);
        m_mode = std::exchange(o.m_mode, OpenMode::NONE);
        return *this;
    }
    bool is_open() const;
    bool is_read() const { return open_mode_is_read(m_mode); }
    bool is_write() const { return open_mode_is_write(m_mode); }
    void open();
    void close();
    void flush();
    size_t read(std::byte* out_bytes, size_t bytes, size_t offset = ~0ull);
    std::string read(size_t bytes = ~0ull, size_t offset = ~0ull);
    bool getline(std::string& str, size_t offset = ~0ull);
    size_t write(const std::byte* bytes, size_t size, size_t offset = ~0ull, bool flush = false);
    void set_read_head(size_t pos) { m_file.seekg(pos, std::ios::beg); }
    void set_write_head(size_t pos) { m_file.seekp(pos, std::ios::beg); }
    void delete_from_disk();
    bool eof() const;
    u64 get_hash();
    auto size() const { return m_size; }
    const Path& path() const { return m_path; }

  private:
    FileSystem* m_fs{};
    std::fstream m_file;
    size_t m_size{};
    Path m_path{};
    OpenMode m_mode{ OpenMode::NONE };
    u64 m_content_hash{};
};

using FilePtr = std::shared_ptr<File>;

class DirectoryListener
{
  public:
    void push_paths(std::span<fs::Path> paths)
    {
        std::scoped_lock lock{ m_mutex };
        for(auto& p : paths)
        {
            if(!m_extension_filter_set.contains(p.extension().string())) { continue; }
            m_changed_files_set.insert(std::move(p));
        }
    }

    void consume_paths(auto&& cb)
    {
        std::scoped_lock lock{ m_mutex };
        if(m_changed_files_set.empty()) { return; }

        auto vec = std::vector<fs::Path>(std::make_move_iterator(m_changed_files_set.begin()),
                                         std::make_move_iterator(m_changed_files_set.end()));
        cb(std::move(vec));
        m_changed_files_set.clear();
    }

    fs::Path m_listening_path;
    std::shared_ptr<void> m_listener;
    std::unordered_set<fs::Path> m_changed_files_set;
    std::unordered_set<StackString<16>> m_extension_filter_set;
    std::mutex m_mutex;
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
    // Checks if file exists
    bool file_exists(const Path& path) const;

    // Create recursive listener for file changes
    Handle<DirectoryListener> make_listener(std::string_view virtual_path, std::span<std::string_view> extensions);

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