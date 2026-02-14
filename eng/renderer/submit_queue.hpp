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

class ICommandBuffer
{
  public:
    virtual ~ICommandBuffer() = default;
    virtual void barrier(Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                         Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access) = 0;
    virtual void barrier(const Image& image, Flags<PipelineStage> src_stage, Flags<PipelineAccess> src_access,
                         Flags<PipelineStage> dst_stage, Flags<PipelineAccess> dst_access, ImageLayout old_layout,
                         ImageLayout new_layout, const ImageMipLayerRange& range = { { 0u, ~0u }, { 0u, ~0u } }) = 0;

    virtual void copy(const Buffer& dst, const Buffer& src, size_t dst_offset, Range range) = 0;
    virtual void copy(const Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count) = 0;
    virtual void copy(const Image& dst, const Image& src, const ImageCopy& copy) = 0;
    virtual void copy(const Image& dst, const Image& src) = 0;
    virtual void blit(const Image& dst, const Image& src, const ImageBlit& blit) = 0;

    virtual void clear_color(const Image& image, const Color4f& color) = 0;
    virtual void clear_depth_stencil(const Image& image, float clear_depth, std::optional<uint32_t> clear_stencil = {}) = 0;

    virtual void bind_index(const Buffer& index, uint32_t offset, VkIndexType type) = 0;
    virtual void bind_pipeline(const Pipeline& pipeline) = 0;
    virtual void bind_sets(const void* sets, uint32_t count = 1) = 0;
    virtual void bind_set(uint32_t slot, std::span<DescriptorResource> resources) = 0;

    virtual void push_constants(Flags<ShaderStage> stages, const void* const values, Range32u range) = 0;

    virtual void set_viewports(const VkViewport* viewports, uint32_t count) = 0;
    virtual void set_scissors(const VkRect2D* scissors, uint32_t count) = 0;

    // todo: hide those?
    virtual void begin_rendering(const VkRenderingInfo& info) = 0;
    virtual void end_rendering() = 0;

    virtual void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset,
                              uint32_t vertex_offset, uint32_t instance_offset) = 0;
    virtual void draw_indexed_indirect_count(const Buffer& indirect, size_t indirect_offset, const Buffer& count,
                                             size_t count_offset, uint32_t max_draw_count, uint32_t stride) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    virtual void begin_label(const std::string& label) = 0;
    virtual void end_label() = 0;
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
                 ImageLayout new_layout, const ImageMipLayerRange& range = { { 0u, ~0u }, { 0u, ~0u } }) override;

    void copy(const Buffer& dst, const Buffer& src, size_t dst_offset, Range range) override;
    void copy(const Image& dst, const Buffer& src, const VkBufferImageCopy2* regions, uint32_t count) override;
    void copy(const Image& dst, const Image& src, const ImageCopy& copy) override;
    void copy(const Image& dst, const Image& src) override;
    void blit(const Image& dst, const Image& src, const ImageBlit& blit) override;

    // color out/color rw -> clear -> color out/color rw/attachment optimal
    void clear_color(const Image& image, const Color4f& color) override;
    // late z /ds rw -> clear -> early z/ds rw/attachment optimal
    void clear_depth_stencil(const Image& image, float clear_depth, std::optional<uint32_t> clear_stencil) override;

    void bind_index(const Buffer& index, uint32_t offset, VkIndexType type) override;
    void bind_pipeline(const Pipeline& pipeline) override;
    void bind_sets(const void* sets, uint32_t count = 1) override;
    void bind_set(uint32_t slot, std::span<DescriptorResource> resources) override;

    void push_constants(Flags<ShaderStage> stages, const void* const values, Range32u range) override;

    void set_viewports(const VkViewport* viewports, uint32_t count) override;
    void set_scissors(const VkRect2D* scissors, uint32_t count) override;

    void begin_rendering(const VkRenderingInfo& info) override;
    void end_rendering() override;

    void before_draw_dispatch();
    void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t vertex_offset, uint32_t instance_offset) override;
    void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t index_offset, uint32_t vertex_offset,
                      uint32_t instance_offset) override;
    // Draw using
    void draw_indexed_indirect_count(const Buffer& indirect, size_t indirect_offset, const Buffer& count,
                                     size_t count_offset, uint32_t max_draw_count, uint32_t stride) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;

    void begin_label(const std::string& label) override;
    void end_label() override;

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
    CommandPoolVk(VkDevice dev, uint32_t family_index, VkCommandPoolCreateFlags flags) noexcept;
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
    uint64_t value{}; // fence & value=1 -> create signaled; bin sem -> ignored; timeline -> set value
    std::string name;
};

struct Sync
{
    using enum SyncType;

    void init(const SyncCreateInfo& info);
    void destroy();
    void signal_cpu(uint64_t value = ~0ull);
    bool wait_cpu(size_t timeout, uint64_t value = ~0ull) const;
    uint64_t signal_gpu(uint64_t value = ~0ull);
    uint64_t wait_gpu(uint64_t value = ~0ull);
    void reset(uint64_t value = 0);
    uint64_t get_next_signal_value() const { return value + 1; }

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
        std::vector<const Sync*> wait_sems;
        std::vector<uint64_t> wait_values;
        std::vector<Flags<PipelineStage>> wait_stages;
        std::vector<Sync*> signal_sems;
        std::vector<uint64_t> signal_values;
        std::vector<Flags<PipelineStage>> signal_stages;
        std::vector<ICommandBuffer*> cmds;
        std::vector<std::pair<Sync*, Flags<PipelineStage>>> pushed_syncs;
        Sync* fence{};
    };

  public:
    SubmitQueue() noexcept = default;
    SubmitQueue(VkDevice dev, VkQueue queue, uint32_t family_idx) noexcept;

    CommandPoolVk* make_command_pool(VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

    SubmitQueue& wait_sync(Sync* sync, Flags<PipelineStage> stages = PipelineStage::ALL, uint64_t value = ~0ull);
    SubmitQueue& signal_sync(Sync* sync, Flags<PipelineStage> stages = PipelineStage::ALL, uint64_t value = ~0ull);
    SubmitQueue& with_cmd_buf(ICommandBuffer* cmd);
    void submit();
    bool submit_wait(uint64_t timeout);
    void present(Swapchain* swapchain);
    void wait_idle();

    VkDevice dev{};
    VkQueue queue{};
    uint32_t family_idx{ ~0u };
    Sync* fence{};
    std::deque<CommandPoolVk> command_pools;
    Submission submission;
};
} // namespace gfx
} // namespace eng
