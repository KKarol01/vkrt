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

enum class OpenMode
{
    DONT_OPEN,
    TRY_READ_BYTES_BEG,              // rb
    TRY_READ_WRITE_BYTES_BEG,        // r+b
    READ_WRITE_BYTES_CREATE_DISCARD, // w+b
    WRITE_BYTES_CREATE_DISCARD,      // wb
};

class FileSystem;

class File
{
  public:
    bool open(const fs::Path& path, OpenMode mode);
    bool reopen();
    void close();
    void flush();
    void delete_from_disk();

    void read(std::byte* dst_bytes, usize dst_size, usize& out_read_bytes, usize src_offset = ~0ull);
    void read(std::string& dst_str, usize max_read_bytes = ~0ull, usize src_offset = ~0ull);
    bool get_line(std::string& dst_str, usize src_offset = ~0ull);
    void write(const std::byte* src_bytes, usize src_size, usize& out_write_bytes, usize dst_offset = ~0ull);

    void set_read_head(usize pos) { m_file.seekg(pos, std::fstream::beg); }
    usize get_read_head() { return (usize)m_file.tellg(); }
    void set_write_head(usize pos) { m_file.seekp(pos, std::fstream::beg); }
    usize get_write_head() { return (usize)m_file.tellp(); }

    bool is_open() const { return m_file.is_open(); }
    bool is_read() const
    {
        return m_mode == OpenMode::TRY_READ_BYTES_BEG || m_mode == OpenMode::TRY_READ_WRITE_BYTES_BEG ||
               m_mode == OpenMode::READ_WRITE_BYTES_CREATE_DISCARD;
    }
    bool is_write() const
    {
        return m_mode == OpenMode::READ_WRITE_BYTES_CREATE_DISCARD || m_mode == OpenMode::WRITE_BYTES_CREATE_DISCARD;
    }
    bool is_eof() const { return m_file.eof(); }

    u64 get_hash();
    usize get_size() const { return m_size; }
    const fs::Path& get_path() const { return m_path; }

  private:
    OpenMode m_mode{ OpenMode::DONT_OPEN };
    fs::Path m_path;
    u64 m_hash{};
    usize m_size{};
    std::fstream m_file;
};

using FilePtr = std::shared_ptr<File>;

class DirectoryListener
{
  public:
    void push_paths(std::span<fs::Path> paths)
    {
        std::scoped_lock lock{ m_mutex };
        m_changed_files_set.insert(std::make_move_iterator(paths.begin()), std::make_move_iterator(paths.end()));
    }

    void consume_paths(std::vector<fs::Path>& out_paths)
    {
        std::scoped_lock lock{ m_mutex };
        out_paths.insert(out_paths.end(), std::make_move_iterator(m_changed_files_set.begin()),
                         std::make_move_iterator(m_changed_files_set.end()));
        m_changed_files_set.clear();
    }

    fs::Path m_listening_path;
    void* m_impl{};
    std::unordered_set<fs::Path> m_changed_files_set;
    std::mutex m_mutex;
};

class FileSystem
{
  public:
    // From virtual path like '/assets/texture.png' make system relative path like '../../assets/texture.png'
    static Path make_rel_path(const Path& virtual_path);
    // Get relative path to the directory where assets are located
    static Path get_assets_path() { return s_root_dir_path; }

    bool init();

    // Open a file with system path (relative or absolute)
    FilePtr open_file(const Path& path, OpenMode mode);
    // Delete file from the disk
    void delete_file(const Path& path);
    // Checks if file exists
    bool file_exists(const Path& path) const;

    // Create recursive listener for file changes
    DirectoryListener* make_listener(std::string_view virtual_path);

    inline static Path s_root_dir_path;
    std::deque<DirectoryListener> m_dir_listeners_vec;
};

} // namespace fs
} // namespace eng