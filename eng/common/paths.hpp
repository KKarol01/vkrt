#pragma once

#include <string>
#include <filesystem>

namespace eng
{
namespace paths
{

inline std::filesystem::path BASE_PATH;
inline std::filesystem::path SHADERS_DIR;
inline std::filesystem::path MODELS_DIR;

inline void init(const char* base_path)
{
    BASE_PATH = std::filesystem::current_path().parent_path(); // this should point to where the assets/ folder is
    SHADERS_DIR = BASE_PATH / "assets" / "shaders";
    MODELS_DIR = BASE_PATH / "assets" / "models";
}

} // namespace paths
} // namespace eng