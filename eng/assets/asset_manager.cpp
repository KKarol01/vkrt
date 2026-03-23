#include "asset_manager.hpp"
#include <eng/engine.hpp>
#include <ranges>

#include <WinBase.h>

namespace eng
{

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
    auto path = make_path(dir);
    if(!path.is_absolute()) { path = std::filesystem::absolute(path); }
    HANDLE fcnh = FindFirstChangeNotificationW(path.c_str(), true, FILE_NOTIFY_CHANGE_LAST_WRITE);
    dir_change_cb_map[dir].handle = fcnh;

    while(true)
    {
        const auto wait_res = WaitForSingleObject(fcnh, 0);
        if(wait_res == WAIT_OBJECT_0)
        {
            uint32_t buffer[1024]{};
            unsigned long rec_bytes{};
            ReadDirectoryChangesW(fcnh, buffer, sizeof(buffer), true, FILE_NOTIFY_CHANGE_LAST_WRITE, &rec_bytes, nullptr, nullptr);
            FILE_NOTIFY_INFORMATION info;
            for(auto i = 0ul; i < rec_bytes; i += sizeof(info))
            {
                memcpy(&info, &buffer[i], sizeof(info));
                std::wstring wfilename(info.FileNameLength / sizeof(wchar_t), L'0' );
                memcpy(wfilename.data(), ((char*)&buffer[i]) + offsetof(FILE_NOTIFY_INFORMATION, FileName), info.FileNameLength);
                std::string filenamestr(wfilename.size(), '0');
                wcstombs(filenamestr.data(), wfilename.data(), filenamestr.size());
                ENG_LOG("File name is {}", filenamestr);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }
}

} // namespace eng