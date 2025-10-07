#pragma once

#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <eng/renderer/renderer.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

namespace eng
{
namespace gfx
{

struct Buffer;
struct Image;
struct Pipeline;
struct Swapchain;

class CommandPool;

class CommandBuffer
{
  public:
    void barrier(Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access, Flags<PipelineStage> dst_stage,
                 Flags<PipelineAccess> dst_access);
    void barrier(Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access, Flags<PipelineStage> dst_stage,
                 Flags<PipelineAccess> dst_access, ImageLayout old_layout, ImageLayout new_layout);

    void copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range range);
    void copy(Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count);
    void copy(Image& dst, const Image& src);

    void clear_color(Image& image, ImageLayout layout, Range mips, Range layers, float color);
    void clear_depth_stencil(Image& image, float clear_depth, uint32_t clear_stencil,
                             ImageLayout layout = ImageLayout::UNDEFINED, Range mips = { 0, ~0u }, Range layers = { 0, ~0u });

    void bind_index(Buffer& index, uint32_t offset, VkIndexType type);
    void bind_pipeline(const Pipeline& pipeline);
    void bind_descriptors(DescriptorPool* ps, DescriptorSet* ds, Range32 range);

    void push_constants(Flags<ShaderStage> stages, const void* const values, Range32 range);
    void bind_resource(uint32_t slot, Handle<Buffer> resource, Range range = { 0, ~0ull });
    void bind_resource(uint32_t slot, Handle<Texture> resource);

    void set_viewports(const VkViewport* viewports, uint32_t count);
    void set_scissors(const VkRect2D* scissors, uint32_t count);

    void begin_rendering(const VkRenderingInfo& info);
    void end_rendering();

    void before_draw_dispatch();

    void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset, uint32_t vertex_offset,
                      uint32_t instance_offset);
    void draw_indexed_indirect_count(Buffer& indirect, size_t indirect_offset, Buffer& count, size_t count_offset,
                                     uint32_t max_draw_count, uint32_t stride);
    void dispatch(uint32_t x, uint32_t y, uint32_t z);

    VkCommandBuffer cmd{};
    const Pipeline* current_pipeline{};
    uint32_t flush_pc_size{};
    std::byte pcbuf[PipelineLayoutCreateInfo::MAX_PUSH_BYTES];
};

class CommandPool
{
  public:
    CommandPool() noexcept = default;
    CommandPool(VkDevice dev, uint32_t family_index, VkCommandPoolCreateFlags flags) noexcept;

    CommandBuffer* allocate();
    CommandBuffer* begin();
    CommandBuffer* begin(CommandBuffer* cmd);
    void end(CommandBuffer* cmd);
    void reset();
    void reset(CommandBuffer* cmd);

    VkDevice dev{};
    std::deque<CommandBuffer> free;
    std::deque<CommandBuffer> used;
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
    uint64_t value{}; // fence & value=1 -> create signaled; bin sem -> ignored; timeline -> set value
    std::string name;
};

struct Sync
{
    using enum SyncType;

    void init(const SyncCreateInfo& info);
    void destroy();
    void signal_cpu(uint64_t value = ~0ull);
    VkResult wait_cpu(size_t timeout, uint64_t value = ~0ull);
    uint64_t signal_gpu(uint64_t value = ~0ull);
    uint64_t wait_gpu(uint64_t value = ~0ull);
    void reset(uint64_t value = 0);

    SyncType type{};
    uint64_t value{};
    std::string name;
    union {
        VkFence fence{};
        VkSemaphore semaphore;
    };
};

class SubmitQueue
{
    struct Submission
    {
        std::vector<Sync*> wait_sems;
        std::vector<uint64_t> wait_values;
        std::vector<Flags<PipelineStage>> wait_stages;
        std::vector<Sync*> signal_sems;
        std::vector<uint64_t> signal_values;
        std::vector<Flags<PipelineStage>> signal_stages;
        std::vector<CommandBuffer*> cmds;
        Sync* fence{};
    };

  public:
    SubmitQueue() noexcept = default;
    SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept;

    CommandPool* make_command_pool(VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

    SubmitQueue& wait_sync(Sync* sync, Flags<PipelineStage> stages = {}, uint64_t value = ~0ull);
    SubmitQueue& signal_sync(Sync* sync, Flags<PipelineStage> stages = {}, uint64_t value = ~0ull);
    SubmitQueue& with_cmd_buf(CommandBuffer* cmd);
    VkResult submit();
    VkResult submit_wait(uint64_t timeout);
    void present(Swapchain* swapchain);
    void wait_idle();

    VkDevice dev{};
    VkQueue queue{};
    uint32_t family_idx{ ~0u };
    Sync* fence{};
    std::deque<CommandPool> command_pools;
    Submission submission;
};
} // namespace gfx
} // namespace eng
