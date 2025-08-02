#pragma once

#include <eng/renderer/submit_queue.hpp>
#include <eng/common/handle.hpp>
#include <eng/renderer/resources/resources.hpp>
#include <vulkan/vulkan.h>
#include <span>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <deque>
#include <mutex>
#include <numeric>
#include <functional>
#include <eng/common/types.hpp>

namespace gfx
{
struct Buffer;

static constexpr auto STAGING_APPEND = ~0ull;

class GPUStagingManager
{
    static constexpr auto CAPACITY = 64ull * 1024 * 1024;
    static constexpr auto ALIGNMENT = 512ull;

  public:
    void init(SubmitQueue* queue, const std::function<void(Handle<Buffer>)>& on_buffer_resize = {});

    void resize(Handle<Buffer> buffer, size_t newsize);
    void copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range range);
    void copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, Range range);
    template <typename T> void copy(Handle<Buffer> dst, const std::vector<T>& vec, size_t dst_offset)
    {
        copy(dst, vec.data(), dst_offset, { 0, vec.size() * sizeof(T) });
    }
    void insert_barrier();
    void copy(Handle<Image> dst, const void* const src, VkImageLayout final_layout);
    void flush();
    void reset();

  private:
    void allocate_new_cmd();
    std::pair<void*, size_t> allocate(size_t size);
    size_t get_offset(const void* const alloc) { return (uintptr_t)alloc - (uintptr_t)buffer.memory; }

    size_t head{};
    Buffer buffer;
    SubmitQueue* queue{};
    CommandPool* cmdpool{};
    CommandBuffer* cmd{};
    std::vector<CommandBuffer*> cmds;
    std::function<void(Handle<Buffer>)> on_buffer_resize;
};

// class StagingBuffer
//{
//     static constexpr auto CAPACITY = 64ull * 1024 * 1024;
//     static constexpr auto ALIGNMENT = 512u;
//
//     struct Transaction
//     {
//         Handle<Buffer> dst_buffer() const;
//         Handle<Image> dst_image() const;
//         Handle<Buffer> src_buffer() const;
//         Handle<Image> src_image() const;
//         uint32_t dst_resource;
//         uint32_t src_resource;
//         size_t dst_offset;
//         Range src_range;
//         bool dst_is_buffer;
//         bool src_is_buffer;
//         const void* alloc{};
//         VkImageLayout final_layout{};
//     };
//
//   public:
//     StagingBuffer(SubmitQueue* queue) noexcept;
//     void stage(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range src_range);
//     void stage(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size);
//     void stage(Handle<Buffer> dst, std::span<const std::byte> src, size_t dst_offset);
//     template <typename T> void stage(Handle<Buffer> dst, const T& src, size_t dst_offset)
//     {
//         stage(dst, &src, dst_offset, sizeof(T));
//     }
//     template <typename T> void stage(Handle<Buffer> dst, const std::vector<T>& src, size_t dst_offset)
//     {
//         stage(dst, src.data(), dst_offset, src.size() * sizeof(T));
//     }
//
//     void stage(Handle<Image> dst, std::span<const std::byte> src, VkImageLayout final_layout);
//     void flush();
//
//   private:
//     void* try_allocate(size_t size);
//     std::pair<void*, size_t> allocate(size_t size);
//     void* head_to_ptr() const { return static_cast<std::byte*>(data) + head; }
//     size_t get_free_space() const { return CAPACITY - head; }
//     size_t align(size_t sz) const { return (sz + ALIGNMENT - 1) & -ALIGNMENT; }
//     size_t resize_buffer(Handle<Buffer> hbuf, size_t dst_offset, size_t src_size);
//     size_t calc_alloc_head(void* ptr) const { return (std::uintptr_t)ptr - (std::uintptr_t)data; }
//     void begin_cmd_buffer();
//     void record_mem_barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
//                             VkAccessFlags2 dst_access);
//     void record_mem_barrier(std::span<const VkImageMemoryBarrier2> barriers);
//     void record_replacement_buffers();
//     void record_copy(Buffer& dst, Buffer& src, size_t dst_offset, Range src_range);
//     void record_copy(Image& dst, size_t src_offset);
//     VkBufferCopy2 create_copy(size_t dst_offset, Range src_range) const;
//     VkImageMemoryBarrier2 create_layout_transition(const Image& img, VkImageLayout src_layout, VkImageLayout dst_layout,
//                                                    VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
//                                                    VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) const;
//
//     Buffer& get_buffer(Handle<Buffer> buffer);
//
//     std::vector<Transaction> transactions;
//     std::vector<std::pair<Handle<Buffer>, Buffer>> replacement_buffers;
//
//     SubmitQueue* queue{};
//     VkFence fence{};
//     CommandPool* cmdpool{};
//     VkCommandBuffer cmd{};
//     Handle<Buffer> buffer{};
//     void* data{};
//     size_t head{};
// };

} // namespace gfx