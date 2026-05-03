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
        : m_virtual_path(virtual_path), m_physical_path(physical_path), m_listener(listener)
    {
    }
    ~Win32DirChangeHandle() { close(); }

    void start()
    {
        m_is_stopped = false;

        m_notification = CreateFileW(m_physical_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if(!m_notification)
        {
            ENG_WARN("[WIN32] Failed to attach on_dir_change notification");
            return;
        }

        m_event = CreateEventA(NULL, false, false, NULL);
        if(!m_event)
        {
            ENG_WARN("[WIN32] Failed to create on_dir_change listener");
            close();
            return;
        }

        m_overlap.hEvent = m_event;

        const auto queued_success = ReadDirectoryChangesW(m_notification, m_buffer, sizeof(m_buffer), true,
                                                          FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &m_overlap, nullptr);
        if(!queued_success)
        {
            ENG_WARN("[WIN32] Failed to queue directory on_dir_change");
            close();
            return;
        }

        m_wait_thread = std::thread{ [this] {
            while(!m_is_stopped)
            {
                const auto wait_res = WaitForSingleObject(m_event, INFINITE);
                if(m_is_stopped) { break; }
                if(wait_res != WAIT_OBJECT_0) { continue; }
                unsigned long rec_bytes{};
                const auto getres = GetOverlappedResult(m_notification, &m_overlap, &rec_bytes, false);
                if(!getres)
                {
                    if(m_is_stopped) { break; }
                    continue;
                }
                std::vector<fs::Path> paths;
                paths.reserve(rec_bytes / sizeof(FILE_NOTIFY_INFORMATION));
                size_t offset = 0;
                FILE_NOTIFY_INFORMATION info{};
                while(offset < rec_bytes)
                {
                    auto* file_info = (char*)m_buffer + offset;
                    memcpy(&info, file_info, sizeof(FILE_NOTIFY_INFORMATION));
                    if(info.FileNameLength > 0)
                    {
                        std::wstring_view filename((wchar_t*)(file_info + offsetof(FILE_NOTIFY_INFORMATION, FileName)),
                                                   info.FileNameLength / sizeof(wchar_t));
                        paths.push_back((m_virtual_path / filename).generic_string());
                    }
                    if(info.NextEntryOffset == 0) { break; }
                    offset += info.NextEntryOffset;
                }

                m_listener->push_paths(paths);

                if(!m_is_stopped)
                {
                    const auto queued = ReadDirectoryChangesW(m_notification, m_buffer, sizeof(m_buffer), true,
                                                              FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &m_overlap, nullptr);
                    if(!queued) { break; }
                }
            }
            CancelIoEx(m_notification, &m_overlap);
        } };
    }

    void close()
    {
        if(!m_notification) { return; }
        m_is_stopped = true;
        CancelIoEx(m_notification, &m_overlap);
        SetEvent(m_event);
        if(m_wait_thread.joinable()) { m_wait_thread.join(); }
        CloseHandle(m_event);
        CloseHandle(m_notification);
        m_notification = {};
        m_event = {};
        m_overlap = {};
    }

    fs::Path m_virtual_path;
    fs::Path m_physical_path;
    HANDLE m_notification{};
    HANDLE m_event{};
    OVERLAPPED m_overlap{};
    Handle<fs::DirectoryListener> m_listener;
    uint32_t m_buffer[sizeof(FILE_NOTIFY_INFORMATION) * 128 / sizeof(uint32_t)]{};
    bool m_is_stopped{};
    std::thread m_wait_thread;
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
    static_assert((int)OpenMode::LAST_ENUM == 5);
    return mode == OpenMode::WRITE_CREATE_BYTES || mode == OpenMode::READ_WRITE_BYTES || mode == OpenMode::READ_WRITE_CREATE_BYTES;
}

File::File(FileSystem* fs, const Path& path, OpenMode mode) : m_fs(fs), m_path(path), m_mode(mode) { open(); }

File::~File() noexcept { close(); }

bool File::is_open() const { return m_file && m_file.is_open(); }

void File::open()
{
    if(m_file.is_open()) { return; }
    if(m_path.empty() || m_mode == OpenMode::NONE) { return; }
    m_file = std::fstream{ m_path.c_str(), open_mode_to_ios(m_mode) };
    if(!m_file.is_open()) { return; }
    if(open_mode_is_read(m_mode))
    {
        m_file.seekg(0, std::ios::end);
        m_size = m_file.tellg();
        m_file.seekg(0, std::ios::beg);
    }
}

void File::close()
{
    m_file.close();
    m_size = 0;
    m_content_hash = {};
}

void File::flush() { m_file.flush(); }

size_t File::read(std::byte* out_bytes, size_t bytes, size_t offset)
{
    if(!m_file.is_open() || !out_bytes) { return 0; }
    if(!is_read()) { return 0; }
    if(offset != ~0ull) { m_file.seekg(offset, std::ios::beg); }
    if(!m_file) { return 0; }
    m_file.read((char*)out_bytes, bytes);
    return m_file.gcount();
}

std::string File::read(size_t bytes, size_t offset)
{
    if(!m_file.is_open()) { return {}; }
    if(!is_read()) { return {}; }
    if(offset == ~0ull) { offset = m_file.tellg(); }
    offset = std::min(m_size, offset);
    bytes = std::min(bytes, m_size - offset);
    std::string str(bytes, '\0');
    const auto read_bytes = read((std::byte*)str.data(), bytes, offset);
    str.resize(read_bytes);
    return str;
}

bool File::getline(std::string& str, size_t offset)
{
    if(offset != ~0ull) { m_file.seekg(offset, std::ios::beg); }
    if(!m_file) { return false; }
    return (bool)std::getline(m_file, str);
}

size_t File::write(const std::byte* bytes, size_t size, size_t offset, bool flush)
{
    if(!m_file || !bytes || size == 0) { return 0; }
    if(!is_write()) { return 0; }
    if(offset != ~0ull) { m_file.seekp(offset, std::ios::beg); }
    if(!m_file) { return 0; }
    m_file.write((const char*)bytes, size);
    if(flush) { this->flush(); }
    return m_file ? size : 0ull;
}

void File::delete_from_disk()
{
    close();
    m_fs->delete_file(m_path);
}

bool File::eof() const { return m_file.eof(); }

uint64_t File::get_hash()
{
    if(m_content_hash != 0) { return m_content_hash; }
    m_content_hash = ENG_HASH(read(m_size), 0);
    set_read_head(0);
    return m_content_hash;
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
    if(!open_mode_is_write(mode) && !std::filesystem::exists(path)) { return {}; }

    const auto hash = std::hash<Path>{}(path);
    auto fileit = m_files_map.find(std::make_pair(hash, mode));
    if(fileit != m_files_map.end())
    {
        auto ptr = fileit->second.lock();
        if(ptr) { return ptr; }
        else { m_files_map.erase(fileit); }
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
    m_files_map.emplace(std::make_pair(hash, mode), sharedfile);
    return sharedfile;
}

void FileSystem::delete_file(const Path& path)
{
    const auto hash = std::hash<Path>{}(path);
    // todo: i don't like this solution
    std::vector<decltype(m_files_map.begin())> its;
    for(auto it = m_files_map.begin(); it != m_files_map.end(); ++it)
    {
        if(it->first.first == hash) { its.push_back(it); }
    }
    for(auto it : its)
    {
        auto ptr = it->second.lock();
        if(ptr) { ptr->close(); }
        m_files_map.erase(it);
    }
    if(!std::filesystem::remove(path)) { ENG_WARN("Could not remove file {}", path.string()); }
}

bool fs::FileSystem::file_exists(const Path& path) const { return std::filesystem::exists(path); }

Handle<DirectoryListener> fs::FileSystem::make_listener(std::string_view virtual_path, std::span<std::string_view> extensions)
{
    ENG_ASSERT(virtual_path.size());
    auto* ptr = &m_dir_listeners_vec.emplace_back();
    ptr->m_listening_path = virtual_path;
    ENG_ASSERT(extensions.size());
    for(auto ext : extensions)
    {
        ENG_ASSERT(ext.starts_with('.') && ext.size() > 0 && ext.size() < 16);
        ptr->m_extension_filter_set.emplace(ext);
    }
    auto physical_path = make_rel_path(virtual_path);
    physical_path = std::filesystem::absolute(physical_path);
    const auto handle = Handle<DirectoryListener>{ (uintptr_t)ptr };
    auto listener = std::make_shared<Win32DirChangeHandle>(virtual_path, physical_path, handle);
    ptr->m_listener = listener;
    listener->start();
    return handle;
}

fs::Path fs::FileSystem::make_rel_path(const fs::Path& virtual_path)
{
    if(virtual_path.string().starts_with("/")) { return root_dir_path / fs::Path{ virtual_path.string().erase(0, 1) }; }
    return virtual_path;
}

fs::FilePtr fs::FileSystem::get_asset(const fs::Path& virtual_path, fs::OpenMode mode)
{
    const auto _path = make_rel_path(virtual_path);
    return open_file(_path, mode);
}

} // namespace fs
} // namespace eng