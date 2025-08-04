#include <eng/common/to_string.hpp>

#include <fmt/format.h>
#include <eng/common/logger.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

namespace eng
{
// clang-format off
std::string to_string(const gfx::ImageFormat& a) 
{ 
    switch(a) 
    {
        case gfx::ImageFormat::R8G8B8A8_UNORM: { return "R8G8B8A8_UNORM"; }
        case gfx::ImageFormat::R8G8B8A8_SRGB: { return "R8G8B8A8_SRGB"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const gfx::ImageType& a) 
{ 
    switch(a) 
    {
        case gfx::ImageType::TYPE_1D: { return "TYPE_1D"; }
        case gfx::ImageType::TYPE_2D: { return "TYPE_2D"; }
        case gfx::ImageType::TYPE_3D: { return "TYPE_3D"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const gfx::ImageViewType& a) 
{ 
    switch(a) 
    {
        case gfx::ImageViewType::TYPE_1D: { return "TYPE_1D"; }
        case gfx::ImageViewType::TYPE_2D: { return "TYPE_2D"; }
        case gfx::ImageViewType::TYPE_3D: { return "TYPE_3D"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const gfx::ImageViewDescriptor& a)
{
    return fmt::format("{}_{}_{}_{}_{}_{}", a.format ? to_string(*a.format) : "EMPTY",
                       a.view_type ? to_string(*a.view_type) : "EMPTY", a.mips.offset, a.mips.size, a.layers.offset,
                       a.layers.size);
}

std::string to_string(const gfx::SyncType& a)
{
    switch(a) 
    {
        case gfx::SyncType::UNKNOWN: { return "UNKNOWN"; }
        case gfx::SyncType::FENCE: { return "FENCE"; }
        case gfx::SyncType::BINARY_SEMAPHORE: { return "BINARY_SEMAPHORE"; }
        case gfx::SyncType::TIMELINE_SEMAPHORE: { return "TIMELINE_SEMAPHORE"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

// clang-format on

} // namespace eng
