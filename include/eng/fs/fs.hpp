#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <deque>
#include <span>
#include <vector>
#include <unordered_set>
#include <string_view>

#include <eng/string/stack_string.hpp>
#include <eng/common/types.hpp>
#include <eng/common/hash.hpp>

namespace eng
{
namespace fs
{

using Path = ::std::filesystem::path;

// Open mode. _BYTES suffix means posix 'b' flag that disables special '\n' handling on windows.
enum class OpenMode
{
    NONE,
    READ_BYTES,              // read from start, fail on no file
    WRITE_CREATE_BYTES,      // write from start, always destroy contents, create new on no file
    READ_WRITE_BYTES,        // read/write from start, error on no file
    READ_WRITE_CREATE_BYTES, // read/write from start, destroy contents, create new on no file
};

class File
{
  public:
    File() = default;
    File(const Path& path, OpenMode mode);

    bool is_open() const { return m_file.is_open(); }
    explicit operator bool() const { return is_open(); }
    bool is_read() const;
    bool is_write() const;
    void close();
    void flush() { m_file.flush(); }
    size_t read(std::byte* out_bytes, size_t bytes, size_t offset = ~0ull);
    std::string read(size_t bytes = ~0ull, size_t offset = ~0ull);
    bool getline(std::string& str);
    size_t write(const std::byte* bytes, size_t size, size_t offset = ~0ull, bool flush = false);
    u64 get_hash();
    size_t size() const { return m_size; }
    const Path& path() const { return m_path; }

  private:
    std::fstream m_file;
    size_t m_size{};
    Path m_path{};
    OpenMode m_mode{ OpenMode::NONE };
    u64 m_content_hash{};
};

class DirectoryListener
{
  public:
    void push_paths(std::span<Path> paths)
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
        cb(std::vector<Path>(std::make_move_iterator(m_changed_files_set.begin()), std::make_move_iterator(m_changed_files_set.end())));
        m_changed_files_set.clear();
    }

    std::shared_ptr<void> m_listener;
    std::unordered_set<Path> m_changed_files_set;
    std::unordered_set<StackString<16>> m_extension_filter_set;
    std::mutex m_mutex;
};

class FileSystem
{
  public:
    bool init(const Path& root);

    File open_file(const Path& path, OpenMode mode);
    void delete_file(const Path& path);
    bool file_exists(const Path& path) const;


    DirectoryListener* make_recursive_directory_listener(const Path& virtual_path, std::span<std::string_view> extensions);

  private:
    std::deque<DirectoryListener> m_dir_listeners;
};

} // namespace fs
} // namespace eng
