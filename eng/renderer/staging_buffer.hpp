#pragma once

#include <eng/renderer/buffer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/handle.hpp>
#include <vulkan/vulkan.h>
#include <span>
#include <vector>
#include <variant>
#include <thread>
#include <atomic>
#include <memory>

class Buffer;
class Image;

class StagingBuffer {
    struct TransferBuffer {
        Handle<Buffer> handle{};
        size_t offset{};
        std::vector<std::byte> data;
    };
    struct TransferImage {
        Handle<Image> handle{};
        size_t offset{};
        std::vector<std::byte> data;
    };

    struct Submission {
        bool started() const { return !transfers.empty(); }
        std::vector<std::variant<TransferBuffer, TransferImage>> transfers;
        VkFence fence{};
    };

  public:
    StagingBuffer() noexcept = default;
    StagingBuffer(SubmitQueue* queue, Handle<Buffer> staging_buffer) noexcept;

    StagingBuffer(StagingBuffer&& o) noexcept;
    StagingBuffer& operator=(StagingBuffer&& o) noexcept;

    template <typename T> StagingBuffer& send_to(Handle<Buffer> buffer, size_t offset, const T& t);
    template <typename T> StagingBuffer& send_to(Handle<Buffer> buffer, size_t offset, const std::vector<T>& ts);
    void submit(VkFence fence = nullptr);
    void submit_wait(VkFence fence = nullptr);

  private:
    Submission& get_submission() { return *submissions[0]; }
    void swap_submissions();
    StagingBuffer& send_to(Handle<Buffer> buffer, size_t offset, std::span<const std::byte> data);
    void resize(Handle<Buffer> buffer, size_t new_size);
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
inline StagingBuffer& StagingBuffer::send_to(Handle<Buffer> buffer, size_t offset, const std::vector<T>& ts) {
    return send_to(buffer, offset, std::as_bytes(std::span{ ts.begin(), ts.end() }));
}
