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

    void allocate();
    void deallocate();
    void resize(size_t sz);

    size_t get_capacity() const { return create_info.size; }
    size_t get_size() const { return size; }
    size_t get_free_space() const { return get_capacity() - get_size(); }

    VkDevice dev{};
    VmaAllocator vma{};
    BufferCreateInfo create_info{};
    VkBuffer buffer{};
    VmaAllocation vma_alloc{};
    VkDeviceAddress bda{};
    void* mapped{};
    size_t size{};
};

} // namespace gfx