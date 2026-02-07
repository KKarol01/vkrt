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
    // auto z = std::filesystem::current_path().string();
    // BASE_PATH = base_path;
    // auto x = BASE_PATH.string();
    // while(!BASE_PATH.parent_path().string().ends_with("vkrt"))
    //{
    //     BASE_PATH = BASE_PATH.parent_path();
    // }

    BASE_PATH = std::filesystem::absolute("./../../../"); // set like this because it's in visual studio out/debug/whatever, and assets are in base folder
    SHADERS_DIR = BASE_PATH / "assets" / "shaders";
    MODELS_DIR = BASE_PATH / "assets" / "models";
}

inline std::filesystem::path canonize_path(std::filesystem::path p)
{
    p.make_preferred();
    return p;
}

} // namespace paths
} // namespace eng