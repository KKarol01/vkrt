#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

class Buffer {
  public:
    Buffer() noexcept = default;
    Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, const VkBufferCreateInfo& create_info,
           const VmaAllocationCreateInfo& alloc_info) noexcept;
    // todo: make destructors and proper move semantics

    std::string name;
    VkDevice dev{};
    VmaAllocator vma{};
    VkBufferCreateInfo create_info; // info.size = capacity
    VmaAllocationCreateInfo alloc_info;
    VkBuffer buffer{};
    VkDeviceAddress bda{};
    void* mapped{};
    VmaAllocation alloc{};
    size_t size{}; // size = size of currently stored data
};