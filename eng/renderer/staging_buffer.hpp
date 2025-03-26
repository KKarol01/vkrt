#pragma once

#include <eng/renderer/buffer.hpp>
#include <vulkan/vulkan.h>

class StagingBuffer {
  public:
    StagingBuffer() noexcept = default;
    StagingBuffer(VkQueue queue, size_t size_bytes) noexcept;

    Buffer staging_buffer;
};