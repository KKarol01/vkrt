#include <eng/common/to_string.hpp>

#include <fmt/format.h>
#include <eng/common/logger.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>

namespace eng
{
namespace gfx
{
// clang-format off
std::string to_string(const ImageFormat& a) 
{ 
    switch(a) 
    {
        case ImageFormat::R8G8B8A8_UNORM: { return "R8G8B8A8_UNORM"; }
        case ImageFormat::R8G8B8A8_SRGB: { return "R8G8B8A8_SRGB"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const ImageType& a) 
{ 
    switch(a) 
    {
        case ImageType::TYPE_1D: { return "TYPE_1D"; }
        case ImageType::TYPE_2D: { return "TYPE_2D"; }
        case ImageType::TYPE_3D: { return "TYPE_3D"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const ImageViewType& a) 
{ 
    switch(a) 
    {
        case ImageViewType::NONE:    { return "NONE"; }
        case ImageViewType::TYPE_1D: { return "TYPE_1D"; }
        case ImageViewType::TYPE_2D: { return "TYPE_2D"; }
        case ImageViewType::TYPE_3D: { return "TYPE_3D"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const SyncType& a)
{
    switch(a) 
    {
        case SyncType::UNKNOWN: { return "UNKNOWN"; }
        case SyncType::FENCE: { return "FENCE"; }
        case SyncType::BINARY_SEMAPHORE: { return "BINARY_SEMAPHORE"; }
        case SyncType::TIMELINE_SEMAPHORE: { return "TIMELINE_SEMAPHORE"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

std::string to_string(const RenderPassType& a)
{
    switch(a)
    {
        case RenderPassType::FORWARD: { return "FORWARD"; }
        case RenderPassType::DIRECTIONAL_SHADOW: { return "DIRECTIONAL_SHADOW"; }
        default: { ENG_ERROR("Unhandled case"); return ""; }
    }
}

// clang-format on

} // namespace gfx
} // namespace eng
