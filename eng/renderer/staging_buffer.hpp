#pragma once

#include <eng/renderer/buffer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <vulkan/vulkan.h>

class StagingBuffer {
  public:
    StagingBuffer() noexcept = default;
    StagingBuffer(VkDevice dev, VmaAllocator vma, SubmitQueue* queue, size_t size_bytes) noexcept;

    VkDevice dev{};
    VmaAllocator vma{};
    SubmitQueue* queue{};
    Buffer staging_buffer;
};