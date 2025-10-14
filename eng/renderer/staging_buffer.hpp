#pragma once

#include <vector>
#include <array>
#include <eng/common/types.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/renderer.hpp>

namespace eng
{
namespace gfx
{

static constexpr auto STAGING_APPEND = ~0ull;

class StagingBuffer
{
    static constexpr auto CAPACITY = 64ull * 1024 * 1024;
    static constexpr auto ALIGNMENT = 512ull;
    static constexpr auto CMD_COUNT = 32ull;

    struct CmdBufWrapper
    {
        enum State
        {
            INITIAL,
            RECORDING,
            EXECUTABLE,
            PENDING
        };
        State state{ INITIAL };
        CommandBuffer* cmd{};
        Sync* sem{};
    };

    struct Allocation
    {
        Buffer* buf{};
        void* mem{};
        size_t offset{};
        size_t size{};
    };

  public:
    void init(SubmitQueue* queue, const Callback<void(Handle<Buffer>)>& on_buffer_resize = {});

    void resize(Handle<Buffer> buffer, size_t newsize);
    void copy(Handle<Buffer> dst, Handle<Buffer> src, size_t dst_offset, Range range);
    void copy(Handle<Buffer> dst, const void* const src, size_t dst_offset, size_t src_size);
    template <typename T> void copy(Handle<Buffer> dst, const std::vector<T>& vec, size_t dst_offset)
    {
        copy(dst, vec.data(), dst_offset, vec.size() * sizeof(T));
    }
    void copy(Handle<Image> dst, const void* const src, ImageLayout final_layout);
    void copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy,
              ImageLayout dst_final_layout);
    void blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit,
              ImageLayout dst_final_layout);
    Sync* flush();
    void reset();

  private:
    void begin_new_cmd_buffer();
    Allocation allocate(size_t size);
    CommandBuffer* get_cmd() { return cmds.at(cmdhead).cmd; }
    CmdBufWrapper& get_wrapper() { return cmds.at(cmdhead); }

    size_t head{};
    Buffer buffer;
    SubmitQueue* queue{};
    Callback<void(Handle<Buffer>)> on_buffer_resize;

    CommandPool* cmdpool{};
    uint32_t cmdhead{ 0 };
    std::array<CmdBufWrapper, CMD_COUNT> cmds;
    std::vector<CmdBufWrapper*> pending;
};

} // namespace gfx
} // namespace eng