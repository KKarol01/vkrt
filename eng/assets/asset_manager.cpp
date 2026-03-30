#include "asset_manager.hpp"
#include <eng/engine.hpp>
#include <ranges>

#include <WinBase.h>

namespace eng
{

struct Win32DirChangeHandle
{
    Win32DirChangeHandle(const fs::Path& virtual_path, const fs::Path& physical_path, Handle<assets::DirectoryListener> listener)
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
    Handle<assets::DirectoryListener> listener;
    uint32_t buffer[sizeof(FILE_NOTIFY_INFORMATION) * 128 / sizeof(uint32_t)]{};
    bool stop{};
    std::thread wait_thread;
};

void assets::AssetManager::init()
{
    static constexpr int MAX_UP = 5;
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
    if(root_dir_path.empty())
    {
        ENG_WARN("Could not find correct directory with eng/ and assets/ dirs in it.");
        return;
    }
}

fs::Path assets::AssetManager::make_path(const fs::Path& path)
{
    if(path.string().starts_with("/")) { return root_dir_path / fs::Path{ path.string().erase(0, 1) }; }
    return path;
}

fs::FilePtr assets::AssetManager::get_asset(const fs::Path& path, fs::OpenMode mode)
{
    const auto _path = make_path(path);
    return get_engine().fs->open_file(_path, mode);
}

void assets::AssetManager::listen_for_path(const fs::Path& virtual_path, Handle<DirectoryListener> listener)
{
    if(!listener) { return; }
    if(virtual_path.empty()) { return; }
    if(!virtual_path.string().starts_with('/')) { return; }
    if(virtual_path.has_extension()) { return; }
    auto physical_path = make_path(virtual_path);
    if(!physical_path.is_absolute()) { physical_path = std::filesystem::absolute(physical_path); }
    auto it = listener->listeners.emplace(virtual_path, nullptr);
    if(it.second)
    {
        auto ptr = std::make_shared<Win32DirChangeHandle>(virtual_path, physical_path, listener);
        it.first->second = ptr;
        ptr->start();
    }
}

} // namespace eng