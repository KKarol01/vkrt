#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <eng/common/hash.hpp>
#include <eng/renderer/renderer.hpp>

namespace gfx
{

struct BufferCreateInfo
{
    auto operator==(const BufferCreateInfo&) const { return false; }
    std::string name;
    size_t size{};
    VkBufferUsageFlags usage{};
    bool mapped{};
};

struct ImageCreateInfo
{
    auto operator==(const ImageCreateInfo&) const { return false; }
    std::string name;
    VkImageType type{};
    VkExtent3D extent{};
    VkFormat format;
    VkImageUsageFlags usage{};
    uint32_t mips{ 1 };
    uint32_t layers{ 1 };
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
};

struct Buffer
{
    constexpr Buffer() noexcept = default;
    explicit Buffer(const BufferCreateInfo& info) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = default;
    Buffer& operator=(Buffer&&) = default;

    bool operator==(const Buffer& b) const;
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
    Image() noexcept = default;
    explicit Image(const ImageCreateInfo& info) noexcept;
    bool operator==(const Image& b) const;
    void init();
    void destroy();
    VkImageAspectFlags deduce_aspect() const;
    VkImageViewType deduce_image_view_type() const;
    // On empty, returns default view. Caches the results.
    VkImageView create_image_view(const ImageViewDescriptor& info = {});
    VkImageView get_image_view(const ImageViewDescriptor& info = {}) const;

    std::string name;
    VkImage image{};
    VmaAllocation vmaa{};
    VkImageType type;
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkExtent3D extent{};
    VkFormat format;
    uint32_t mips{};
    uint32_t layers{};
    VkImageUsageFlags usage;
    std::unordered_map<ImageViewDescriptor, VkImageView> views;
};

} // namespace gfx

DEFINE_STD_HASH(gfx::Buffer, eng::hash::combine_fnv1a(t.buffer, t.vmaa, t.bda, t.usage, t.capacity, t.size, t.memory, t.mapped));
DEFINE_STD_HASH(gfx::Image, eng::hash::combine_fnv1a(t.image, t.vmaa, t.extent.width, t.extent.height, t.extent.depth,
                                                     t.format, t.mips, t.layers, t.usage));
