#pragma once

#include <optional>
#include <eng/fs/fs.hpp>
#include <eng/assets/asset_manager.hpp>

namespace eng
{
namespace assets
{

class AssetLoaderGLTF
{
  public:
    static std::optional<Asset> load_from_file(const fs::Path& path);
};

} // namespace assets
} // namespace eng