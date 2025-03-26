#pragma once

#include <eng/renderer/buffer.hpp>
#include <eng/renderer/vk_cmd_queue.hpp>
#include <vulkan/vulkan.h>

class StagingBuffer {
  public:
    StagingBuffer() noexcept = default;
    StagingBuffer(VkDevice dev, VmaAllocator vma, VkSubmitQueue* queue, size_t size_bytes) noexcept;

    VkDevice dev{};
    VmaAllocator vma{};
    VkSubmitQueue* queue{};
    Buffer staging_buffer;
};