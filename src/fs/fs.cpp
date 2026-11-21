#include <eng/fs/fs.hpp>

#include <thread>
#include <eng/common/logger.hpp>

#ifdef ENG_PLATFORM_WIN32
#include <WinBase.h>
#endif

namespace eng
{
namespace fs
{

static std::ios::openmode to_ios(OpenMode mode)
{
    switch(mode)
    {
    case OpenMode::READ_BYTES:
        return std::ios::binary | std::ios::in;
    case OpenMode::WRITE_CREATE_BYTES:
        return std::ios::binary | std::ios::out | std::ios::trunc;
    case OpenMode::READ_WRITE_BYTES:
        return std::ios::binary | std::ios::in | std::ios::out;
    case OpenMode::READ_WRITE_CREATE_BYTES:
        return std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc;
    default:
        return {};
    }
}

// '/assets/a.png' -> 'assets/a.png', which is relative to the root dir (the working directory).
static Path to_physical(const Path& path) { return path.relative_path(); }

#ifdef ENG_PLATFORM_WIN32
struct Win32DirChangeHandle
{
    Win32DirChangeHandle(const Path& virtual_path, DirectoryListener* listener)
        : m_virtual_path(virtual_path), m_listener(listener)
    {
    }
    ~Win32DirChangeHandle() { close(); }

    void start()
    {
        m_is_stopped = false;

        const auto physical_path = std::filesystem::absolute(to_physical(m_virtual_path));
        m_notification = CreateFileW(physical_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if(m_notification == INVALID_HANDLE_VALUE)
        {
            m_notification = {};
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
                if(!GetOverlappedResult(m_notification, &m_overlap, &rec_bytes, false)) { continue; }

                std::vector<Path> paths;
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
        if(m_event) { SetEvent(m_event); }
        if(m_wait_thread.joinable()) { m_wait_thread.join(); }
        if(m_event) { CloseHandle(m_event); }
        CloseHandle(m_notification);
        m_notification = {};
        m_event = {};
        m_overlap = {};
    }

    Path m_virtual_path;
    HANDLE m_notification{};
    HANDLE m_event{};
    OVERLAPPED m_overlap{};
    DirectoryListener* m_listener{};
    u32 m_buffer[sizeof(FILE_NOTIFY_INFORMATION) * 128 / sizeof(u32)]{};
    bool m_is_stopped{};
    std::thread m_wait_thread;
};
#endif

File::File(const Path& path, OpenMode mode) : m_file(to_physical(path), to_ios(mode)), m_path(path), m_mode(mode)
{
    if(m_file.is_open() && is_read())
    {
        m_file.seekg(0, std::ios::end);
        m_size = m_file.tellg();
        m_file.seekg(0, std::ios::beg);
    }
}

bool File::is_read() const { return m_mode != OpenMode::NONE && m_mode != OpenMode::WRITE_CREATE_BYTES; }

bool File::is_write() const { return m_mode != OpenMode::NONE && m_mode != OpenMode::READ_BYTES; }

void File::close()
{
    m_file.close();
    m_size = 0;
    m_content_hash = 0;
}

size_t File::read(std::byte* out_bytes, size_t bytes, size_t offset)
{
    if(!is_open() || !is_read() || !out_bytes) { return 0; }
    if(offset != ~0ull) { m_file.seekg(offset, std::ios::beg); }
    if(!m_file) { return 0; }
    m_file.read((char*)out_bytes, bytes);
    return m_file.gcount();
}

std::string File::read(size_t bytes, size_t offset)
{
    if(!is_open() || !is_read()) { return {}; }
    if(offset == ~0ull) { offset = m_file.tellg(); }
    offset = std::min(m_size, offset);
    bytes = std::min(bytes, m_size - offset);
    std::string str(bytes, '\0');
    str.resize(read((std::byte*)str.data(), bytes, offset));
    return str;
}

bool File::getline(std::string& str) { return m_file && (bool)std::getline(m_file, str); }

size_t File::write(const std::byte* bytes, size_t size, size_t offset, bool flush)
{
    if(!is_open() || !is_write() || !bytes || size == 0) { return 0; }
    if(offset != ~0ull) { m_file.seekp(offset, std::ios::beg); }
    if(!m_file) { return 0; }
    m_file.write((const char*)bytes, size);
    if(flush) { m_file.flush(); }
    return m_file ? size : 0ull;
}

u64 File::get_hash()
{
    if(m_content_hash != 0) { return m_content_hash; }
    m_content_hash = ENG_HASH(read(m_size, 0), 0);
    m_file.clear();
    m_file.seekg(0, std::ios::beg);
    return m_content_hash;
}

bool FileSystem::init(const Path& root)
{
    if(!root.empty())
    {
        std::error_code ec;
        std::filesystem::current_path(root, ec);
        if(ec)
        {
            ENG_ERROR("Could not set root directory to {}: {}", root.string(), ec.message());
            return false;
        }
    }
    if(!std::filesystem::is_directory("assets"))
    {
        ENG_ERROR("Root directory {} has no assets/ directory. Pass the correct one with --root <dir>.",
                  std::filesystem::current_path().string());
        return false;
    }
    return true;
}

File FileSystem::open_file(const Path& path, OpenMode mode) { return File{ path, mode }; }

void FileSystem::delete_file(const Path& path)
{
    std::error_code ec;
    std::filesystem::remove(to_physical(path), ec);
    if(ec) { ENG_WARN("Could not remove file {}: {}", path.string(), ec.message()); }
}

bool FileSystem::file_exists(const Path& path) const { return std::filesystem::exists(to_physical(path)); }

DirectoryListener* FileSystem::make_recursive_directory_listener(const Path& virtual_path, std::span<std::string_view> extensions)
{
    ENG_ASSERT(!virtual_path.empty() && !extensions.empty());
    auto* ptr = &m_dir_listeners.emplace_back();
    for(auto ext : extensions)
    {
        ENG_ASSERT(ext.starts_with('.') && ext.size() < 16);
        ptr->m_extension_filter_set.emplace(ext);
    }
#ifdef ENG_PLATFORM_WIN32
    auto listener = std::make_shared<Win32DirChangeHandle>(virtual_path, ptr);
    listener->start();
    ptr->m_listener = listener;
#else
    ENG_WARN("Directory listening is not implemented on this platform");
#endif
    return ptr;
}

} // namespace fs
} // namespace eng
