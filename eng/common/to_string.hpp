#pragma once

#include <string>

namespace gfx
{
enum class ImageFormat;
enum class ImageType;
enum class ImageViewType;
enum class SyncType;
struct ImageViewDescriptor;
} // namespace gfx

struct VkImageViewCreateInfo;

namespace eng
{

std::string to_string(const gfx::ImageFormat& a);
std::string to_string(const gfx::ImageType& a);
std::string to_string(const gfx::ImageViewType& a);
std::string to_string(const gfx::ImageViewDescriptor& a);
std::string to_string(const gfx::SyncType& a);

} // namespace eng