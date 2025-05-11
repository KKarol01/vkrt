#pragma once

#include <eng/renderer/submit_queue.hpp>
#include <eng/common/handle.hpp>
#include <vulkan/vulkan.h>
#include <span>
#include <vector>
#include <variant>
#include <thread>
#include <atomic>
#include <memory>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)

namespace gfx {

class StagingBuffer {
    struct TransferBuffer {
        Handle<Buffer> handle{};
        size_t offset{};
        std::vector<std::byte> data;
    };
    struct TransferImage {
        Handle<Image> handle{};
        VkImageLayout final_layout{};
        VkBufferImageCopy2 region{};
        std::vector<std::byte> data;
    };
    struct TransferFromBuffer {
        Handle<Buffer> dst_buffer{};
        size_t dst_offset{};
        Handle<Buffer> src_buffer{};
        size_t src_offset{};
        size_t size{};
    };

    struct Submission {
        bool started() const { return !transfers.empty(); }
        std::vector<std::variant<TransferBuffer, TransferImage, TransferFromBuffer>> transfers;
        VkFence fence{};
    };

  public:
    StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept;

    StagingBuffer(StagingBuffer&& o) noexcept;
    StagingBuffer& operator=(StagingBuffer&& o) noexcept;

    template <typename T> StagingBuffer& send_to(Handle<Buffer> buffer, size_t offset, const T& t);
    template <typename T> StagingBuffer& send_to(Handle<Buffer> buffer, size_t offset, const std::vector<T>& ts);
    template <typename T>
    StagingBuffer& send_to(Handle<Image> image, VkImageLayout final_layout, const VkBufferImageCopy2 region,
                           const std::vector<T>& ts);
    StagingBuffer& send_to(Handle<Buffer> dst_buffer, size_t dst_offset, Handle<Buffer> src_buffer, size_t src_offset, size_t size);
    void submit(VkFence fence = nullptr);
    void submit_wait(VkFence fence = nullptr);

  private:
    Submission& get_submission() { return *submissions[0]; }
    void swap_submissions();
    StagingBuffer& send_to(Handle<Buffer> buffer, size_t offset, const std::span<const std::byte> ts);
    StagingBuffer& send_to(Handle<Image> image, VkImageLayout final_layout, const VkBufferImageCopy2 region,
                           const std::span<const std::byte> ts);
    void resize(Handle<Buffer> buffer, size_t new_size);
    VkImageMemoryBarrier2 generate_image_barrier(Handle<Image> image, VkImageLayout layout, bool is_final_layout);
    void transition_image(Handle<Image> image, VkImageLayout layout, bool is_final_layout);
    void process_submission();
    size_t push_data(const std::vector<std::byte>& data);

    SubmitQueue* queue{};
    CommandPool* cmdpool{};
    VkCommandBuffer cmds[2]{};
    Buffer* staging_buffer{};
    std::unique_ptr<Submission> submissions[2]{};
    std::jthread on_submit_complete_thread;
    std::atomic_bool submission_done{ true };
    VkFence submission_thread_fence{};
};

template <typename T> inline StagingBuffer& StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, const T& t) {
    return send_to(buffer, offset, std::as_bytes(std::span{ &t, 1 }));
}

template <typename T>
inline StagingBuffer& StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, const std::vector<T>& ts) { // todo: maybe vector to span<T>
    return send_to(buffer, offset, std::as_bytes(std::span{ ts.begin(), ts.end() }));
}

template <typename T>
inline StagingBuffer& StagingBuffer::send_to(Handle<Image> image, VkImageLayout final_layout,
                                             const VkBufferImageCopy2 region, const std::vector<T>& ts) {
    return send_to(image, final_layout, region, std::as_bytes(std::span{ ts.begin(), ts.end() }));
}

} // namespace gfx
