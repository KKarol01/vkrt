#pragma once

#include <eng/renderer/buffer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/handle.hpp>
#include <vulkan/vulkan.h>

class StagingBuffer {
  public:
    StagingBuffer() noexcept = default;
    StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept;

    SubmitQueue* queue{};
    Handle<Buffer> staging_buffer;
};