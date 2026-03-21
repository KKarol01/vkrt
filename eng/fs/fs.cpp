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
    content = {};
    content_hash = {};
}

void File::read(std::byte* out_bytes, size_t bytes, size_t offset)
{
    if(!out_bytes) { return; }
    if(file && fseek((FILE*)file, this->offset + offset, SEEK_SET) == 0)
    {
        const auto readchars = fread_s(out_bytes, bytes, 1, bytes, (FILE*)file);
        ENG_ASSERT(readchars == bytes);
        return;
    }
}

void File::write(const std::byte* bytes, size_t size, size_t offset)
{
    if(!file || !bytes || size == 0) { return; }
    size_t writechars = 0;
    if(fseek((FILE*)file, this->offset + offset, SEEK_SET) == 0)
    {
        writechars = (size_t)fwrite(bytes, 1, size, (FILE*)file);
    }
    ENG_ASSERT(writechars == size);
}

std::string_view File::read()
{
    if(!file) { return {}; }
    if(!content.empty()) { return content; }

    content.resize(size);
    const auto readchars = fread(content.data(), 1, size, (FILE*)file);
    ENG_ASSERT(readchars == size);
    return content;
}

uint64_t File::get_hash()
{
    if(content_hash != 0) { return content_hash; }
    if(content.empty()) { read(); }
    content_hash = hash::combine_fnv1a(content);
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

    FILE* rawfile{};
    fopen_s(&rawfile, path.string().c_str(), open_mode_to_posix(mode));

    if(!rawfile) { return {}; }

    size_t size = 0;
    if(mode != OpenMode::WB)
    {
        fseek(rawfile, 0, SEEK_END);
        size = static_cast<size_t>(ftell(rawfile));
        fseek(rawfile, 0, SEEK_SET);
    }

    auto sharedfile = std::make_shared<File>(this, (void*)rawfile, 0ull, size, path);
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