#pragma once

#include <string>
#include <eng/renderer/types.hpp>

namespace eng
{
namespace gfx
{

std::string to_string(const ImageFormat& a);
std::string to_string(const ImageType& a);
std::string to_string(const ImageViewType& a);
std::string to_string(const ImageLayout& a);
std::string to_string(const SyncType& a);
std::string to_string(const MeshPassType& a);
std::string to_string(const Flags<PipelineStage>& a);
std::string to_string(const Flags<PipelineAccess>& a);

} // namespace gfx
} // namespace eng