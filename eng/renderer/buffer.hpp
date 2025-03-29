#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

struct buffer_resizable_t {};
static constexpr buffer_resizable_t buffer_resizable;

class Buffer {
  public:
    Buffer() noexcept = default;
    Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, const VkBufferCreateInfo& vk_info,
           const VmaAllocationCreateInfo& vma_info) noexcept;
    Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, buffer_resizable_t resizable,
           const VkBufferCreateInfo& vk_info, const VmaAllocationCreateInfo& vma_info) noexcept;
    // todo: make destructors and proper move semantics

    size_t capacity() const { return vk_info.size; }
    size_t size() const { return _size; }
    size_t free_space() const { return capacity() - size(); }

    std::string name;
    VkDevice dev{};
    VmaAllocator vma{};
    VkBufferCreateInfo vk_info; // info.size = capacity
    VmaAllocationCreateInfo vma_info;
    VkBuffer buffer{};
    VkDeviceAddress bda{};
    void* mapped{};
    VmaAllocation alloc{};
    size_t _size{}; // size = size of currently stored data
    bool is_resizable{};
};