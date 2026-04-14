#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <map>
#include <string_view>
#include <eng/common/hash.hpp>

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

class FileSystem
{
  public:
    FilePtr open_file(const Path& path, OpenMode mode);
    void delete_file(const Path& path);

    std::map<std::pair<PathHash, OpenMode>, std::weak_ptr<File>> filemap;
};

} // namespace fs
} // namespace eng