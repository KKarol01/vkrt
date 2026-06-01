#pragma once

#include <eng/renderer/renderer.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

namespace eng
{
namespace gfx
{
class CommandPoolVk;
struct DescriptorSetVk;
class IDescriptorSetAllocator;
struct DescriptorResource;
struct QueryPoolMetadataVk;

class ICommandBuffer
{
  public:
    virtual ~ICommandBuffer() = default;
    virtual void barrier(Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                         Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access) = 0;
    virtual void barrier(const Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                         Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access, ImageLayout old_layout,
                         ImageLayout new_layout, const ImageMipsLayers& range = { { 0u, ~0u }, { 0u, ~0u } }) = 0;

    virtual void copy(const Buffer& dst, const Buffer& src, size_t dst_offset, Range64u range) = 0;
    virtual void copy(const Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, u32 count) = 0;
    virtual void copy(const Image& dst, const Image& src, const ImageCopy& copy) = 0;
    virtual void copy(const Image& dst, const Image& src) = 0;
    virtual void blit(const Image& dst, const Image& src, const ImageBlit& blit) = 0;

    virtual void clear_color(const Image& image, const Color4f& color) = 0;
    virtual void clear_depth_stencil(const Image& image, float clear_depth, std::optional<u32> clear_stencil = {}) = 0;

    virtual void bind_index(const Buffer& index, u32 offset, VkIndexType type) = 0;
    virtual void bind_pipeline(const Pipeline& pipeline) = 0;
    virtual void bind_resources(u32 slot, std::span<DescriptorResource> resources) = 0;

    virtual void push_constants(Flags<ShaderStage> stages, const void* const values, Range32u range) = 0;

    virtual void set_viewports(const VkViewport* viewports, u32 count) = 0;
    virtual void set_scissors(const VkRect2D* scissors, u32 count) = 0;

    // todo: hide those?
    virtual void begin_rendering(const VkRenderingInfo& info) = 0;
    virtual void end_rendering() = 0;

    virtual void draw(u32 vertex_count, u32 instance_count, u32 vertex_offset, u32 instance_offset) = 0;
    virtual void draw_indexed(u32 index_count, u32 instance_count, u32 index_offset, u32 vertex_offset, u32 instance_offset) = 0;
    virtual void draw_indexed_indirect_count(const Buffer& indirect, size_t indirect_offset, const Buffer& count,
                                             size_t count_offset, u32 max_draw_count, u32 stride) = 0;
    virtual void dispatch(u32 x, u32 y, u32 z) = 0;

    virtual void begin_label(const char* label) = 0;
    virtual void end_label() = 0;
    virtual void reset_query_indices(QueryPool* pool, u32 query_index, u32 count) = 0;
    virtual void write_timestamp(QueryPool* pool, Flags<PipelineStage> stage, u32 index) = 0;

    virtual void wait_sync(Sync* sync, u64 wait_value, Flags<PipelineStage> stage = PipelineStage::ALL);
    virtual void wait_sync(Sync* sync, Flags<PipelineStage> stage = PipelineStage::ALL);
    virtual void signal_sync(Sync* sync, Flags<PipelineStage> stage = PipelineStage::ALL);

    virtual void generate_mips(Image& img);

    struct SyncDep
    {
        Sync* sync{};
        u64 value;
        Flags<PipelineStage> stage;
        bool wait{}; // or signal
    };
    std::vector<SyncDep> sync_deps;
    std::array<std::span<DescriptorResource>, PipelineLayout::MAX_SETS> descs_to_bind;
};

class CommandBufferVk : public ICommandBuffer
{
  public:
    CommandBufferVk(VkCommandBuffer cmd, IDescriptorSetAllocator* setalloc) : cmd(cmd), descriptor_allocator(setalloc)
    {
    }
    ~CommandBufferVk() override = default;

    void barrier(Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access, Flags<PipelineStage> dst_stage,
                 Flags<PipelineAccess> dst_access) override;
    void barrier(const Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                 Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access, ImageLayout old_layout,
                 ImageLayout new_layout, const ImageMipsLayers& range = { { 0u, ~0u }, { 0u, ~0u } }) override;

    void copy(const Buffer& dst, const Buffer& src, size_t dst_offset, Range64u range) override;
    void copy(const Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, u32 count) override;
    void copy(const Image& dst, const Image& src, const ImageCopy& copy) override;
    void copy(const Image& dst, const Image& src) override;
    void blit(const Image& dst, const Image& src, const ImageBlit& blit) override;

    // color out/color rw -> clear -> color out/color rw/attachment optimal
    void clear_color(const Image& image, const Color4f& color) override;
    // late z /ds rw -> clear -> early z/ds rw/attachment optimal
    void clear_depth_stencil(const Image& image, float clear_depth, std::optional<u32> clear_stencil) override;

    void bind_index(const Buffer& index, u32 offset, VkIndexType type) override;
    void bind_pipeline(const Pipeline& pipeline) override;
    void bind_sets(const void* sets, u32 count = 1);
    void bind_resources(u32 slot, std::span<DescriptorResource> resources) override;

    void push_constants(Flags<ShaderStage> stages, const void* const values, Range32u range) override;

    void set_viewports(const VkViewport* viewports, u32 count) override;
    void set_scissors(const VkRect2D* scissors, u32 count) override;

    void begin_rendering(const VkRenderingInfo& info) override;
    void end_rendering() override;

    void before_draw_dispatch();
    void draw(u32 vertex_count, u32 instance_count, u32 vertex_offset, u32 instance_offset) override;
    void draw_indexed(u32 index_count, u32 instance_count, u32 index_offset, u32 vertex_offset, u32 instance_offset) override;
    // Draw using
    void draw_indexed_indirect_count(const Buffer& indirect, size_t indirect_offset, const Buffer& count,
                                     size_t count_offset, u32 max_draw_count, u32 stride) override;
    void dispatch(u32 x, u32 y, u32 z) override;

    void begin_label(const char* label) override;
    void end_label() override;
    void reset_query_indices(QueryPool* pool, u32 query_index, u32 count) override;
    void write_timestamp(QueryPool* pool, Flags<PipelineStage> stage, u32 index) override;

    VkCommandBuffer cmd{};
    const Pipeline* current_pipeline{};
    IDescriptorSetAllocator* descriptor_allocator{};
    bool rebind_desc_sets{ true };
};

class ICommandPool
{
  public:
    virtual ~ICommandPool() = default;
    virtual ICommandBuffer* allocate() = 0;
    virtual ICommandBuffer* begin() = 0;
    virtual ICommandBuffer* begin(ICommandBuffer* cmd) = 0;
    virtual void end(ICommandBuffer* cmd) = 0;
    virtual void reset() = 0;
    virtual void reset(ICommandBuffer* cmd) = 0;
};

class CommandPoolVk : public ICommandPool
{
  public:
    CommandPoolVk() noexcept = default;
    CommandPoolVk(VkDevice dev, u32 family_index, VkCommandPoolCreateFlags flags) noexcept;
    ~CommandPoolVk() override = default;

    ICommandBuffer* allocate() override;
    ICommandBuffer* begin() override;
    ICommandBuffer* begin(ICommandBuffer* cmd) override;
    void end(ICommandBuffer* cmd) override;
    void reset() override;
    void reset(ICommandBuffer* cmd) override;

    VkDevice dev{};
    std::deque<CommandBufferVk> free;
    std::deque<CommandBufferVk> used;
    VkCommandPool pool{};
};

enum class SyncType
{
    UNKNOWN,
    FENCE,
    BINARY_SEMAPHORE,
    TIMELINE_SEMAPHORE
};

struct SyncCreateInfo
{
    SyncType type{};
    u64 value{}; // fence & value=1 -> create signaled; bin sem -> ignored; timeline -> set value
    std::string name;
};

struct Sync
{
    using enum SyncType;

    void init(const SyncCreateInfo& info);
    void destroy();
    void signal_cpu(u64 value = ~0ull);
    bool wait_cpu(size_t timeout, u64 value = ~0ull) const;
    u64 signal_gpu(u64 value = ~0ull);
    u64 wait_gpu(u64 value = ~0ull);
    void reset(u64 value = 0);
    u64 get_next_signal_value() const { return value + 1; }
    u64 get_current_wait_value() const { return value; }

    SyncType type{};
    u64 value{};
    std::string name;
    union {
        VkFence fence{};
        VkSemaphore semaphore;
    };
};

struct QueryPoolCreateInfo
{
    QueryType type{};
    u32 max_queries{};
};

struct QueryPool
{
    // Ring-buffer-like allocation with wraparound.
    u32 allocate_queries(u32 count)
    {
        // Get proposal offset
        auto offset = index.load();
        const auto overflow_desired = count; // cas will set to count, meaning it will allocate 2 right out.
        while(true)
        {
            auto next = offset + count;
            // if oob, wraparound
            if(next > max_queries) { next = overflow_desired; }
            // try commiting allocation, on fail, update offset and try again
            if(index.compare_exchange_weak(offset, next))
            {
                offset = next - count; // if cas succeeds, it doesn't update offset, so it still may be max_queries, when next already will have been count.
                ENG_ASSERT(offset + count <= max_queries);
                return offset;
            }
        }
    }
    QueryType type{};
    u32 max_queries{};
    std::atomic_uint32_t index{};
    struct Metadata
    {
        QueryPoolMetadataVk* vk() const { return (QueryPoolMetadataVk*)ptr; }
        void* ptr{};
    } md{};
};

class SubmitQueue
{
    struct Submission
    {
        std::vector<const Sync*> wait_sems;
        std::vector<u64> wait_values;
        std::vector<Flags<PipelineStage>> wait_stages;
        std::vector<Sync*> signal_sems;
        std::vector<u64> signal_values;
        std::vector<Flags<PipelineStage>> signal_stages;
        std::vector<ICommandBuffer*> cmds;
        Sync* fence{};
    };

  public:
    SubmitQueue() noexcept = default;
    SubmitQueue(VkDevice dev, VkQueue queue, u32 family_idx) noexcept;

    CommandPoolVk* make_command_pool(VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

    SubmitQueue& wait_sync(Sync* sync, Flags<PipelineStage> stages = PipelineStage::ALL, u64 value = ~0ull);
    SubmitQueue& signal_sync(Sync* sync, Flags<PipelineStage> stages = PipelineStage::ALL, u64 value = ~0ull);
    SubmitQueue& with_cmd_buf(ICommandBuffer* cmd);
    void submit();
    bool submit_wait(u64 timeout);
    void present(Swapchain* swapchain);
    void wait_idle();

    VkDevice dev{};
    VkQueue queue{};
    u32 family_idx{ ~0u };
    Sync* fence{};
    std::deque<CommandPoolVk> command_pools;
    Submission submission;
};
} // namespace gfx
} // namespace eng
