#pragma once

#include <string>
#include <eng/renderer/renderer_fwd.hpp>

namespace eng
{
namespace gfx
{
std::string to_string(const ImageFormat& a);
std::string to_string(const ImageType& a);
std::string to_string(const ImageViewType& a);
std::string to_string(const SyncType& a);
std::string to_string(const RenderPassType& a);
} // namespace gfx
} // namespace eng