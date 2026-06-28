#include "fs.hpp"

#include <cstdio>
#include <eng/common/logger.hpp>

#ifdef ENG_PLATFORM_WIN32
#include <WinBase.h>
#endif

static int open_mode_to_ios(eng::fs::OpenMode mode)
{
    switch(mode)
    {
    case eng::fs::OpenMode::TRY_READ_BYTES_BEG:
    {
        return std::ios::binary | std ::ios::in;
    }
    case eng::fs::OpenMode::TRY_READ_WRITE_BYTES_BEG:
    {
        return std::ios::binary | std ::ios::in | std::ios::out;
    }
    case eng::fs::OpenMode::READ_WRITE_BYTES_CREATE_DISCARD:
    {
        return std::ios::binary | std ::ios::in | std::ios::out | std::ios::trunc;
    }
    case eng::fs::OpenMode::WRITE_BYTES_CREATE_DISCARD:
    {
        return std::ios::binary | std::ios::out | std::ios::trunc;
    }
    default:
    {
        return 0;
    }
    }
}

namespace eng
{

#ifdef ENG_PLATFORM_WIN32
struct Win32DirChangeHandle
{
    Win32DirChangeHandle(const fs::Path& virtual_path, const fs::Path& physical_path, fs::DirectoryListener* listener)
        : m_virtual_path(virtual_path), m_physical_path(physical_path), m_listener(listener)
    {
    }
    ~Win32DirChangeHandle() { close(); }

    void start()
    {
        m_is_stopped = false;

        m_notification = CreateFileW(m_physical_path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
        if(!m_notification || m_notification == INVALID_HANDLE_VALUE)
        {
            ENG_WARN("[WIN32] Failed to attach on_dir_change notification");
            return;
        }

        m_event = CreateEventA(NULL, false, false, NULL);
        if(m_event == NULL)
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

        m_wait_thread = std::jthread{ [this](std::stop_token stop_token) {
            while(!stop_token.stop_requested())
            {
                const auto wait_res = WaitForSingleObject(m_event, INFINITE);
                if(stop_token.stop_requested()) { break; }
                if(wait_res != WAIT_OBJECT_0) { continue; }
                unsigned long rec_bytes{};
                const auto getres = GetOverlappedResult(m_notification, &m_overlap, &rec_bytes, false);
                if(!getres) { continue; }
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
    fs::DirectoryListener* m_listener;
    u32 m_buffer[sizeof(FILE_NOTIFY_INFORMATION) * 128 / sizeof(u32)]{};
    bool m_is_stopped{};
    std::jthread m_wait_thread;
};
#endif

namespace fs
{

bool File::open(const fs::Path& path, OpenMode mode)
{
    ENG_ASSERT(!m_file.is_open());
    m_path = path;
    m_mode = mode;
    m_file = std::fstream{ path.c_str(), open_mode_to_ios(mode) };
    if(is_open()) { m_size = std::filesystem::file_size(m_path); }
    return is_open();
}

bool File::reopen()
{
    ENG_ASSERT(!m_path.empty() && m_mode != OpenMode::DONT_OPEN);
    return open(m_path, m_mode);
}

void File::close()
{
    if(is_open()) { m_file.close(); }
    // m_path = fs::Path{};
    // m_mode = OpenMode::DONT_OPEN;
    m_hash = 0;
    m_size = 0;
}

void File::flush() { m_file.flush(); }

void File::delete_from_disk()
{
    if(!m_path.empty()) { auto ret = std::filesystem::remove(m_path); }
}

void File::read(std::byte* dst_bytes, usize dst_size, usize& out_read_bytes, usize src_offset)
{
    if(dst_bytes == nullptr || !is_read() || !is_open())
    {
        out_read_bytes = 0;
        return;
    }
    if(src_offset != ~0ull) { set_read_head(src_offset); }
	src_offset = get_read_head();
    out_read_bytes = std::min(dst_size, m_size - src_offset);
    m_file.read((char*)dst_bytes, out_read_bytes);
    out_read_bytes = m_file.gcount();
}

void File::read(std::string& dst_str, usize max_read_bytes, usize src_offset)
{
    dst_str.clear();
    if(!is_read() || !is_open()) { return; }
    if(src_offset == ~0ull) { src_offset = get_read_head(); }
    auto read_bytes = std::min(max_read_bytes, m_size - src_offset);
    dst_str.resize(read_bytes);
    read((std::byte*)dst_str.data(), read_bytes, read_bytes, src_offset);
}

bool File::get_line(std::string& dst_str, usize src_offset)
{
    dst_str.clear();
    if(!is_read() || !is_open()) { return false; }
    return (bool)std::getline(m_file, dst_str);
}

void File::write(const std::byte* src_bytes, usize src_size, usize& out_write_bytes, usize dst_offset)
{
    out_write_bytes = 0;
    if(!src_bytes || src_size == 0) { return; }
    if(dst_offset != ~0ull) { set_write_head(dst_offset); }
    m_file.write((const char*)src_bytes, src_size);
    if(m_file.good()) { out_write_bytes = src_size; }
}

u64 File::get_hash()
{
    if(m_hash != 0) { return m_hash; }
    if(is_read() && is_open())
    {
        std::string str;
        read(str);
        set_read_head(0);
        m_hash = ENG_HASH(str);
    }
    return m_hash;
}

bool FileSystem::init()
{
    s_root_dir_path = "./";
    const auto has_assets_dir = file_exists(make_rel_path("/assets"));
    if(!has_assets_dir) { ENG_ERROR("assets folder missing in cwd directory."); }
    return has_assets_dir;
}

FilePtr FileSystem::open_file(const Path& path, OpenMode mode)
{
    auto rel_path = make_rel_path(path);
    auto file = std::shared_ptr<File>{ new File{}, [](File* ptr) {
                                          ptr->close();
                                          delete ptr;
                                      } };
    file->open(rel_path, mode);
    return file;
}

void FileSystem::delete_file(const Path& path)
{
    std::filesystem::remove(path);
    if(file_exists(path)) { ENG_WARN("Could not remove file {}", path.string()); }
}

bool fs::FileSystem::file_exists(const Path& path) const { return std::filesystem::exists(path); }

DirectoryListener* fs::FileSystem::make_listener(std::string_view virtual_path)
{
    ENG_ASSERT(virtual_path.size());
    auto* dir_listener = &m_dir_listeners_vec.emplace_back();
    dir_listener->m_listening_path = virtual_path;
    auto physical_path = make_rel_path(virtual_path);
    physical_path = std::filesystem::absolute(physical_path);
    auto listener_impl = new Win32DirChangeHandle{ virtual_path, physical_path, dir_listener };
    dir_listener->m_impl = listener_impl;
    listener_impl->start();
    return dir_listener;
}

fs::Path fs::FileSystem::make_rel_path(const fs::Path& virtual_path)
{
    if(virtual_path.string().starts_with("/"))
    {
        auto str = virtual_path.string();
        return s_root_dir_path / fs::Path{ str.begin() + 1, str.end() };
    }
    return virtual_path;
}

} // namespace fs
} // namespace eng