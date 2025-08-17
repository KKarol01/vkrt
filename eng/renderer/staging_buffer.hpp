#pragma once

#include <eng/renderer/submit_queue.hpp>
#include <eng/common/handle.hpp>
#include <vulkan/vulkan.h> // todo: remove this from here
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
    static constexpr auto CMD_COUNT = 32ull;

    struct CmdBufWrapper
    {
        CommandBuffer* cmd{};
        Sync* sem{};
    };

  public:
    void init(SubmitQueue* queue, const std::function<void(Handle<Buffer>)>& on_buffer_resize = {});

    void resize(Handle<Buffer> buffer, size_t newsize);
    void copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range range);
    void copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, Range range);
    template <typename T> void copy(Handle<Buffer> dst, const std::vector<T>& vec, size_t dst_offset)
    {
        copy(dst, vec.data(), dst_offset, { 0, vec.size() * sizeof(T) });
    }
    void copy(Handle<Image> dst, const void* const src, ImageLayout final_layout);
    Sync* flush();
    void reset();

  private:
    Sync* flush_pending();
    void begin_new_cmd_buffer();
    std::pair<void*, size_t> allocate(size_t size);
    size_t get_offset(const void* const alloc) const { return (uintptr_t)alloc - (uintptr_t)buffer.memory; }
    uint32_t get_cmd_index() const;
    CmdBufWrapper& get_wrapped_cmd();
    CommandBuffer* get_cmd();

    size_t head{};
    Buffer buffer;
    SubmitQueue* queue{};
    std::function<void(Handle<Buffer>)> on_buffer_resize;

    CommandPool* cmdpool{};
    uint32_t cmdstart{ 0 };
    uint32_t cmdcount{ 0 };
    std::array<CmdBufWrapper, CMD_COUNT> cmds;
};

} // namespace gfx