#pragma once

#include <vector>
#include <deque>
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

    struct Transaction
    {
        enum class State
        {
            UNINITIALIZED,
            INITIAL,
            RECORDING,
            PENDING
        };
        State state{ State::UNINITIALIZED };
        CommandBuffer* cmd{};
        Sync* sync{};
    };

    struct Allocation
    {
        Transaction* transaction{};
        Buffer* buf{};
        void* mem{};
        size_t offset{};
        size_t realsize{}; // actual size
        size_t size{};     // min of realsize and user-requested size
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
    void copy(Handle<Image> dst, Handle<Image> src, const ImageCopy& copy, ImageLayout dst_final_layout);
    void blit(Handle<Image> dst, Handle<Image> src, const ImageBlit& blit, ImageLayout dst_final_layout);
    Sync* flush();
    void reset();

  private:
    Allocation allocate(size_t size);
    Transaction& get_transaction();
    CommandBuffer* get_cmd();
    Sync* get_sync();

    size_t head{};
    Buffer buffer;
    SubmitQueue* queue{};
    Callback<void(Handle<Buffer>)> on_buffer_resize;

    CommandPool* cmdpool{};
    Sync* dummy_sync{};
    std::deque<Transaction> transactions;
    std::vector<Allocation> allocations;
    std::deque<Sync*> syncs;
    std::deque<CommandBuffer*> cmds;
    std::vector<Transaction*> staged;
    std::vector<Allocation> free_allocs;
};

} // namespace gfx
} // namespace eng