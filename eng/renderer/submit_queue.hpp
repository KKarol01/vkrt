#pragma once

#include <eng/common/handle.hpp>
#include <eng/common/types.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <deque>

namespace gfx
{

struct Buffer;
struct Image;
struct Pipeline;

class CommandPool;

class CommandBuffer
{
  public:
    void barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access);
    void barrier(Image& image, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                 VkAccessFlags2 dst_access, VkImageLayout old_layout, VkImageLayout new_layout);

    void copy(Buffer& dst, const Buffer& src, size_t dst_offset, Range range);
    void copy(Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count);

    void clear_color(Image& image, VkImageLayout layout, Range mips, Range layers, float color);
    void clear_depth_stencil(Image& image, VkImageLayout layout, Range mips, Range layers, float clear_depth, uint32_t clear_stencil);

    void bind_index(Buffer& index, uint32_t offset, VkIndexType type);
    void bind_pipeline(const Pipeline& pipeline);
    void bind_descriptors(VkDescriptorSet* sets, Range range);

    void push_constants(VkShaderStageFlags stages, const void* const values, Range range);

    void set_viewports(const VkViewport* viewports, uint32_t count);
    void set_scissors(const VkRect2D* scissors, uint32_t count);

    void begin_rendering(const VkRenderingInfo& info);
    void end_rendering();
    void draw_indexed_indirect_count(Buffer& indirect, size_t indirect_offset, Buffer& count, size_t count_offset,
                                     uint32_t max_draw_count, uint32_t stride);
    void dispatch(uint32_t x, uint32_t y, uint32_t z);

    VkCommandBuffer cmd{};
    const Pipeline* current_pipeline{};
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

class SubmitQueue
{
    struct Submission
    {
        VkFence fence{};
        std::vector<VkCommandBufferSubmitInfo> cmds;
        std::vector<VkSemaphoreSubmitInfo> wait_sems;
        std::vector<VkSemaphoreSubmitInfo> sig_sems;
    };

  public:
    SubmitQueue() noexcept = default;
    SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept;

    CommandPool* make_command_pool(VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    VkFence make_fence(bool signaled);
    VkSemaphore make_semaphore();
    void reset_fence(VkFence fence);
    void destroy_fence(VkFence fence);
    VkResult wait_fence(VkFence fence, uint64_t timeout);

    SubmitQueue& with_fence(VkFence fence);
    SubmitQueue& with_wait_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    SubmitQueue& with_sig_sem(VkSemaphore sem, VkPipelineStageFlags2 stages);
    SubmitQueue& with_cmd_buf(CommandBuffer* cmd);
    VkResult submit();
    VkResult submit_wait(uint64_t timeout);
    void wait_idle();

    VkDevice dev{};
    VkQueue queue{};
    uint32_t family_idx{ ~0u };
    std::deque<CommandPool> command_pools;
    std::deque<VkSemaphore> semaphores;
    std::deque<VkFence> fences;
    Submission submission;
};

} // namespace gfx
