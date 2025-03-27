#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>

StagingBuffer::StagingBuffer(VkDevice dev, VmaAllocator vma, SubmitQueue* queue, size_t size_bytes) noexcept
    : dev(dev), vma(vma), queue(queue) {
    if(!dev || !vma || !queue) { return; }
    staging_buffer =
        Buffer{ "staging_buffer", dev, vma,
                Vks(VkBufferCreateInfo{ .size = size_bytes, .usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR }),
                VmaAllocationCreateInfo{
                    .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                    .usage = VMA_MEMORY_USAGE_AUTO,
                    .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT } };
}
