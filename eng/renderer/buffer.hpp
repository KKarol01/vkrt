#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

namespace gfx
{

struct BufferCreateInfo
{
    std::string name;
    VkBufferUsageFlags usage{};
    size_t size{};
    bool mapped{ false };
};

class Buffer
{
  public:
    Buffer() noexcept = default;
    Buffer(VkDevice dev, VmaAllocator vma, const BufferCreateInfo& create_info) noexcept;
    Buffer(Buffer&& o) noexcept;
    Buffer& operator=(Buffer&& o) noexcept;

    void allocate();
    void deallocate();

    size_t get_free_space() const { return capacity - size; }

    std::string name;
    VkDevice dev{};
    VkBuffer buffer{};
    VmaAllocator vma{};
    VmaAllocation vmaalloc{};
    VkDeviceAddress bda{};
    VkBufferUsageFlags usage{};
    size_t capacity{};
    size_t size{};
    void* memory{};
    uint32_t bindless_index{ ~0ul };
    bool mapped{};
};

} // namespace gfx