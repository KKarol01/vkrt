#include "fs.hpp"

#include <cstdio>
#include <eng/common/logger.hpp>

#ifdef ENG_PLATFORM_WIN32
#include <WinBase.h>
#endif

// static const char* open_mode_to_posix(eng::fs::OpenMode mode)
//{
//     switch(mode)
//     {
//     case eng::fs::OpenMode::READ_BYTES:
//     {
//         return "rb";
//     }
//     case eng::fs::OpenMode::WRITE_CREATE_BYTES:
//     {
//         return "wb";
//     }
//     case eng::fs::OpenMode::READ_WRITE_BYTES:
//     {
//         return "r+b";
//     }
//     case eng::fs::OpenMode::READ_WRITE_CREATE_BYTES:
//     {
//         return "w+b";
//     }
//         return "";
//     }
// }

static int open_mode_to_ios(eng::fs::OpenMode mode)
{
    switch(mode)
    {
    case eng::fs::OpenMode::READ_BYTES:
    {
        return std::ios::binary | std::ios::in;
    }
    case eng::fs::OpenMode::WRITE_CREATE_BYTES:
    {
        return std::ios::binary | std::ios::out | std::ios::trunc;
    }
    case eng::fs::OpenMode::READ_WRITE_BYTES:
    {
        return std::ios::binary | std::ios::in | std::ios::out;
    }
    case eng::fs::OpenMode::READ_WRITE_CREATE_BYTES:
    {
        return std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc;
    }
        return 0;
    }
}

namespace eng
{

#ifdef ENG_PLATFORM_WIN32
struct Win32DirChangeHandle
{
    Win32DirChangeHandle(const fs::Path& virtual_path, const fs::Path& physical_path, Handle<fs::DirectoryListener> listener)
        : virtual_path(virtual_path), physical_path(physical_path), listener(listener)
    {
    }
    ~Win32DirChangeHandle() { close(); }

    void start()
    {
        stop = false;

        notification = CreateFileW(physical_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if(!notification)
        {
            ENG_WARN("[WIN32] Failed to attach on_dir_change notification");
            return;
        }

        event = CreateEventA(NULL, false, false, NULL);
        if(!event)
        {
            ENG_WARN("[WIN32] Failed to create on_dir_change listener");
            close();
            return;
        }

        overlap.hEvent = event;

        const auto queued_success = ReadDirectoryChangesW(notification, buffer, sizeof(buffer), true,
                                                          FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &overlap, nullptr);
        if(!queued_success)
        {
            ENG_WARN("[WIN32] Failed to queue directory on_dir_change");
            close();
            return;
        }

        wait_thread = std::thread{ [this] {
            while(!stop)
            {
                const auto wait_res = WaitForSingleObject(event, INFINITE);
                if(stop) { break; }
                if(wait_res != WAIT_OBJECT_0) { continue; }
                unsigned long rec_bytes{};
                const auto getres = GetOverlappedResult(notification, &overlap, &rec_bytes, false);
                if(!getres)
                {
                    if(stop) { break; }
                    continue;
                }
                std::vector<fs::Path> paths;
                paths.reserve(rec_bytes / sizeof(FILE_NOTIFY_INFORMATION));
                size_t offset = 0;
                FILE_NOTIFY_INFORMATION info{};
                while(offset < rec_bytes)
                {
                    auto* file_info = (char*)buffer + offset;
                    memcpy(&info, file_info, sizeof(FILE_NOTIFY_INFORMATION));
                    if(info.FileNameLength > 0)
                    {
                        std::wstring_view filename((wchar_t*)(file_info + offsetof(FILE_NOTIFY_INFORMATION, FileName)),
                                                   info.FileNameLength / sizeof(wchar_t));
                        paths.push_back((virtual_path / filename).generic_string());
                    }
                    if(info.NextEntryOffset == 0) { break; }
                    offset += info.NextEntryOffset;
                }

                listener->add_paths(paths);

                if(!stop)
                {
                    const auto queued = ReadDirectoryChangesW(notification, buffer, sizeof(buffer), true,
                                                              FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &overlap, nullptr);
                    if(!queued) { break; }
                }
            }
            CancelIoEx(notification, &overlap);
        } };
    }

    void close()
    {
        if(!notification) { return; }
        stop = true;
        CancelIoEx(notification, &overlap);
        SetEvent(event);
        if(wait_thread.joinable()) { wait_thread.join(); }
        CloseHandle(event);
        CloseHandle(notification);
        notification = {};
        event = {};
        overlap = {};
    }

    fs::Path virtual_path;
    fs::Path physical_path;
    HANDLE notification{};
    HANDLE event{};
    OVERLAPPED overlap{};
    Handle<fs::DirectoryListener> listener;
    uint32_t buffer[sizeof(FILE_NOTIFY_INFORMATION) * 128 / sizeof(uint32_t)]{};
    bool stop{};
    std::thread wait_thread;
};
#endif

namespace fs
{

bool open_mode_is_read(OpenMode mode)
{
    static_assert((int)OpenMode::LAST_ENUM == 5);
    return mode == OpenMode::READ_BYTES || mode == OpenMode::READ_WRITE_BYTES || mode == OpenMode::READ_WRITE_CREATE_BYTES;
}

bool open_mode_is_write(OpenMode mode)
{
    return mode == OpenMode::WRITE_CREATE_BYTES || mode == OpenMode::READ_WRITE_BYTES || mode == OpenMode::READ_WRITE_CREATE_BYTES;
}

File::File(FileSystem* fs, const Path& path, OpenMode mode) : fs(fs), path(path), mode(mode)
{
    if(path.empty() || mode == OpenMode::NONE) { return; }
    file = std::fstream{ path.c_str(), open_mode_to_ios(mode) };
    if(!file.is_open()) { return; }
    if(open_mode_is_read(mode))
    {
        file.seekg(0, std::ios::end);
        size = file.tellg();
        file.seekg(0, std::ios::beg);
    }
}

File::~File() noexcept { close(); }

bool File::is_open() const { return file && file.is_open(); }

void File::close()
{
    file.close();
    size = 0;
    content_hash = {};
}

size_t File::read(std::byte* out_bytes, size_t bytes, size_t offset)
{
    if(!file.is_open() || !out_bytes) { return 0; }
    if(!open_mode_is_read(mode)) { return 0; }
    if(offset != ~0ull) { file.seekg(offset, std::ios::beg); }
    if(!file) { return 0; }
    file.read((char*)out_bytes, bytes);
    return file.gcount();
}

std::string File::read(size_t bytes, size_t offset)
{
    if(!file.is_open()) { return {}; }
    if(!open_mode_is_read(mode)) { return {}; }
    if(offset == ~0ull) { file.tellg(); }
    if(!file) { return {}; }
    offset = std::min(size, offset);
    bytes = std::min(bytes, size - offset);
    std::string str(bytes, '\0');
    const auto read_bytes = read((std::byte*)str.data(), bytes, offset);
    str.resize(read_bytes);
    return str;
}

size_t File::write(const std::byte* bytes, size_t size, size_t offset)
{
    if(!file || !bytes || size == 0) { return 0; }
    if(!open_mode_is_write(mode)) { return 0; }
    if(offset != ~0ull) { file.seekp(offset, std::ios::beg); }
    if(!file) { return 0; }
    file.write((const char*)bytes, size);
    return file ? size : 0ull;
}

void File::delete_from_disk()
{
    close();
    fs->delete_file(path);
}

bool File::eof() const { return file.eof(); }

uint64_t File::get_hash()
{
    if(content_hash != 0) { return content_hash; }
    content_hash = hash::fnv1a_list(read(size, 0));
    return content_hash;
}

bool FileSystem::init()
{
    static constexpr int MAX_UP = 3;
    std::filesystem::path cwd = "./";
    for(int i = 0; i < MAX_UP; ++i)
    {
        const auto found_dir =
            std::ranges::any_of(std::filesystem::directory_iterator{ cwd }, [](const std::filesystem::directory_entry& e) {
                return e.exists() && e.is_directory() && e.path().string().ends_with("assets");
            });
        if(found_dir)
        {
            root_dir_path = cwd;
            break;
        }
        else { cwd += "../"; }
    }
    if(root_dir_path.empty()) { ENG_WARN("Could not find correct directory with eng/ and assets/ dirs in it."); }
    return !root_dir_path.empty();
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
    auto tries = 0u;

    auto sharedfile = std::make_shared<File>(this, path, mode);
    while(!sharedfile->is_open() && tries++ < 10)
    {
        ENG_ASSERT(false); // todo: check if this loop ever runs; it was needed because listening for file changes sometimes blocks opening of files...
        std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
        *sharedfile = File{ this, path, mode };
    }
    if(!sharedfile->is_open()) { return {}; }
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

Handle<fs::DirectoryListener> fs::FileSystem::make_listener()
{
    auto* ptr = &dir_listeners.emplace_back();
    return Handle<DirectoryListener>{ reinterpret_cast<uintptr_t>(ptr) };
}

void fs::FileSystem::listen_for_path(const fs::Path& virtual_path, Handle<DirectoryListener> listener)
{
    if(!listener) { return; }
    if(virtual_path.empty()) { return; }
    if(!virtual_path.string().starts_with('/')) { return; }
    if(virtual_path.has_extension()) { return; }
    auto physical_path = make_rel_path(virtual_path);
    if(!physical_path.is_absolute()) { physical_path = std::filesystem::absolute(physical_path); }
    auto it = listener->listeners.emplace(virtual_path, nullptr);
    if(it.second)
    {
        auto ptr = std::make_shared<Win32DirChangeHandle>(virtual_path, physical_path, listener);
        it.first->second = ptr;
        ptr->start();
    }
}

fs::Path fs::FileSystem::make_rel_path(const fs::Path& path)
{
    if(path.string().starts_with("/")) { return root_dir_path / fs::Path{ path.string().erase(0, 1) }; }
    return path;
}

fs::FilePtr fs::FileSystem::get_asset(const fs::Path& path, fs::OpenMode mode)
{
    const auto _path = make_rel_path(path);
    return open_file(_path, mode);
}

} // namespace fs
} // namespace eng