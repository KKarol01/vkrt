#pragma once

#include <filesystem>

namespace paths {
static std::filesystem::path canonize_path(std::filesystem::path p, const std::filesystem::path& subdir = "") {
    p = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / subdir / p;
    p.make_preferred();
    return p;
}
} // namespace paths