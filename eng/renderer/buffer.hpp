#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

class Buffer {
  public:
    Buffer() noexcept = default;
    Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, const VkBufferCreateInfo& vk_info,
           const VmaAllocationCreateInfo& vma_info) noexcept;
    // todo: make destructors and proper move semantics

    std::string name;
    VkDevice dev{};
    VmaAllocator vma{};
    VkBufferCreateInfo vk_info; // info.size = capacity
    VmaAllocationCreateInfo vma_info;
    VkBuffer buffer{};
    VkDeviceAddress bda{};
    void* mapped{};
    VmaAllocation alloc{};
    size_t size{}; // size = size of currently stored data
};