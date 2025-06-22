#pragma once

#include <string>
#include <optional>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

namespace gfx
{

struct BufferCreateInfo
{
    std::string name;
    size_t size{};
    VkBufferUsageFlags usage{};
    bool mapped{};
};

struct ImageCreateInfo
{
    std::string name;
    VkExtent3D extent{};
    VkFormat format;
    VkImageUsageFlags usage{};
    uint32_t mips{ 1 };
    uint32_t layers{ 1 };
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

struct ImageViewCreateInfo
{
    std::string name;
    std::optional<VkImageViewType> view_type;
    std::optional<VkFormat> format;
    std::optional<VkImageSubresourceRange> range;
    VkComponentMapping swizzle{};
};

struct Buffer
{
    constexpr Buffer() noexcept = default;
    explicit Buffer(const BufferCreateInfo& info) noexcept;
    void init();
    void destroy();

    std::string name;
    VkBuffer buffer{};
    VmaAllocation vmaa{};
    VkDeviceAddress bda{};
    VkBufferUsageFlags usage{};
    size_t capacity{};
    size_t size{};
    void* memory{};
    bool mapped{};
};

struct Image
{
    constexpr Image() noexcept = default;
    explicit Image(const ImageCreateInfo& info) noexcept;
    void init();
    void destroy();
    VkImageAspectFlags deduce_aspect() const;
    VkImageType deduce_image_type() const;
    VkImageViewType deduce_image_view_type() const;
    VkImageView create_image_view(const ImageViewCreateInfo& info);

    std::string name;
    VkImage image{};
    VmaAllocation vmaa{};
    VkImageView default_view{};
    VkImageLayout current_layout{};
    VkExtent3D extent{};
    VkFormat format;
    uint32_t mips{};
    uint32_t layers{};
    VkImageUsageFlags usage;
};

} // namespace gfx