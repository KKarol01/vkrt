#include "asset_manager.hpp"
#include <eng/engine.hpp>
#include <ranges>

#include <WinBase.h>

namespace eng
{

struct Win32DirChangeHandle
{
    Win32DirChangeHandle(const fs::Path& path, Signal<void(const fs::Path&)>* signal) : path(path), signal(signal) {}
    ~Win32DirChangeHandle() { close(); }

    void start()
    {
        stop = false;

        notification = FindFirstChangeNotificationW(path.c_str(), true, FILE_NOTIFY_CHANGE_LAST_WRITE);
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
                FILE_NOTIFY_INFORMATION info{};
                for(size_t offset = 0; rec_bytes > 0; offset += info.NextEntryOffset)
                {
                    memcpy(&info, &buffer[offset], sizeof(info));
                    std::wstring wstr(info.FileNameLength, L'\0');
                    memcpy(wstr.data(), ((char*)&buffer[offset] + offsetof(FILE_NOTIFY_INFORMATION, FileName)),
                           wstr.size() * sizeof(wstr[0]));
                    std::filesystem::path p{ wstr };
                    signal->signal(p);
                    if(info.NextEntryOffset == 0) { break; }
                }

                if(!stop)
                {
                    const auto queued = ReadDirectoryChangesW(notification, buffer, sizeof(buffer), true,
                                                              FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &overlap, nullptr);
                    if(!queued) { break; }
                }
            }
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

    fs::Path path;
    HANDLE notification{};
    HANDLE event{};
    OVERLAPPED overlap{};
    Signal<void(const fs::Path&)>* signal{};
    uint32_t buffer[1024 / 4]{};
    bool stop{};
    std::thread wait_thread;
};

void AssetManager::init()
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
            assets_dir_path = cwd;
            break;
        }
        else { cwd += "../"; }
    }
    if(assets_dir_path.empty())
    {
        ENG_WARN("Could not find correct directory with eng/ and assets/ dirs in it.");
        return;
    }
}

fs::Path AssetManager::make_path(const fs::Path& path)
{
    if(path.string().starts_with("/")) { return assets_dir_path / fs::Path{ path.string().erase(0, 1) }; }
    return path;
}

fs::FilePtr AssetManager::get_asset(const fs::Path& path, fs::OpenMode mode)
{
    const auto _path = make_path(path);
    return get_engine().fs->open_file(_path, mode);
}

void AssetManager::install_notify_on_dir_change_callback(const fs::Path& dir)
{
    if(dir.empty()) { return; }
    if(!dir.string().starts_with('/')) { return; }
    if(dir.has_extension()) { return; }

    auto path = make_path(dir);
    if(!path.is_absolute()) { path = std::filesystem::absolute(path); }

    auto& dir_callback = dir_change_cb_map[path];
    auto ptr = std::make_shared<Win32DirChangeHandle>(path, &dir_callback.on_dir_change);
    dir_callback.handle = ptr;
    ptr->start();
}

} // namespace eng