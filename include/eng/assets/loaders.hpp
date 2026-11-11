#pragma once

#include <optional>
#include <eng/fs/fs.hpp>
#include <eng/assets/asset_manager.hpp>
#include <eng/common/flags.hpp>
#include <eng/renderer/renderer_fwd.hpp>

namespace eng
{
namespace assets
{

enum class ImportSettings
{
    KEEP_DATA_BIT = 0x1, // stores loaded images, vertices, and all the data needed to serialize the asset into custom format
};
ENG_ENABLE_FLAGS_OPERATORS(ImportSettings);

class AssetLoaderGLTF
{
  public:
    static std::optional<Asset> load_from_file(const fs::Path& path, Flags<ImportSettings> import_settings = {});
};

} // namespace assets
} // namespace eng