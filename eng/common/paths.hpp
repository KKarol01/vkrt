#pragma once

#include <filesystem>

namespace eng
{
namespace paths
{

inline static const std::filesystem::path SHADERS_DIR = "shaders";
inline static const std::filesystem::path MODELS_DIR = "models";

static std::filesystem::path canonize_path(std::filesystem::path p)
{
    p = std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / p;
    p.make_preferred();
    return p;
}



} // namespace paths
} // namespace eng