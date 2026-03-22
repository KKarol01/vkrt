#pragma once

#include <eng/fs/fs.hpp>

namespace eng
{
class AssetManager
{
  public:
    void init();
    bool was_properly_init() const { return !assets_dir_path.empty(); }

	fs::Path make_path(const fs::Path& path);
    fs::FilePtr get_asset(const fs::Path& path, fs::OpenMode mode);

    fs::Path assets_dir_path;
};
} // namespace eng