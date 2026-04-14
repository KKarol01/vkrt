#include "fs.hpp"

#include <cstdio>
#include <eng/common/logger.hpp>

static const char* open_mode_to_posix(eng::fs::OpenMode mode)
{
    switch(mode)
    {
    case eng::fs::OpenMode::RB:
    {
        return "rb";
    }
    case eng::fs::OpenMode::WB:
    {
        return "wb";
    }
    case eng::fs::OpenMode::RWB:
    {
        return "r+b";
    }
        return "";
    }
}

namespace eng
{
namespace fs
{

File::~File() noexcept { close(); }

void File::close()
{
    if(file) { fclose((FILE*)file); }
    file = nullptr;
    content_hash = {};
}

size_t File::read(std::byte* out_bytes, size_t bytes, size_t offset)
{
    if(!out_bytes) { return 0ull; }
    if(file && fseek((FILE*)file, offset, SEEK_SET) == 0) { return fread_s(out_bytes, bytes, 1, bytes, (FILE*)file); }
}

std::string File::read(size_t bytes, size_t offset)
{
    offset = std::min(size, offset);
    bytes = std::min(bytes, size - offset);
    std::string str(bytes, '\0');
    const auto read_bytes = read((std::byte*)str.data(), bytes, offset);
    str.resize(read_bytes);
    return str;
}

void File::write(const std::byte* bytes, size_t size, size_t offset)
{
    if(!file || !bytes || size == 0) { return; }
    size_t writechars = 0;
    if(fseek((FILE*)file, offset, SEEK_SET) == 0) { writechars = (size_t)fwrite(bytes, 1, size, (FILE*)file); }
    ENG_ASSERT(writechars == size);
}

uint64_t File::get_hash()
{
    if(content_hash != 0) { return content_hash; }
    content_hash = hash::combine_fnv1a(read(size, 0));
    return content_hash;
}

FilePtr FileSystem::open_file(const Path& path, OpenMode mode)
{
    const auto hash = std::hash<Path>{}(path);
    auto fileit = filemap.find(std::make_pair(hash, mode));
    if(fileit != filemap.end())
    {
        auto ptr = fileit->second.lock();
        if(ptr) { return ptr; }
        else { filemap.erase(fileit); }
    }

    const auto pathstr = path.string();
    FILE* rawfile{};
    const auto EACCESS_ERRNO = 13; // sometimes i get access violation and cannot open the file.
                                   // i suspect this is because of file listening in asset_manager.
    auto tries = 0u;
    while(fopen_s(&rawfile, pathstr.c_str(), open_mode_to_posix(mode)) == EACCESS_ERRNO && tries++ < 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
    }

    if(!rawfile) { return {}; }

    size_t size = 0;
    if(mode != OpenMode::WB)
    {
        fseek(rawfile, 0, SEEK_END);
        size = static_cast<size_t>(ftell(rawfile));
        fseek(rawfile, 0, SEEK_SET);
    }

    auto sharedfile = std::make_shared<File>(this, (void*)rawfile, size, path);
    filemap.emplace(std::make_pair(hash, mode), sharedfile);
    return sharedfile;
}

void FileSystem::delete_file(const Path& path)
{
    const auto hash = std::hash<Path>{}(path);
    // todo: i don't like this solution
    std::vector<decltype(filemap.begin())> its;
    for(auto it = filemap.begin(); it != filemap.end(); ++it)
    {
        if(it->first.first == hash) { its.push_back(it); }
    }
    for(auto it : its)
    {
        auto ptr = it->second.lock();
        if(ptr) { ptr->close(); }
        filemap.erase(it);
    }
    if(!std::filesystem::remove(path)) { ENG_WARN("Could not remove file {}", path.string()); }
}

} // namespace fs
} // namespace eng