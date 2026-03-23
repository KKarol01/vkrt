#pragma once

namespace eng
{
namespace gfx
{

struct RendererBackendCaps
{
    bool supports_bindless{};
};

struct RendererBackendLimits
{
    float timestampPeriodNs{};
};

struct RendererMemoryRequirements
{
    auto operator<=>(const RendererMemoryRequirements&) const = default;
    size_t size{};
    size_t alignment{};
    uintptr_t backend_data{}; // for storing additional backend-specific data (vulkan uses it to store memory type bits)
};

class IRendererBackend
{
  public:
    virtual ~IRendererBackend() = default;

    virtual void init() = 0;

    virtual void allocate_buffer(Buffer& buffer, AllocateMemory alloc = AllocateMemory::YES) = 0;
    virtual void destroy_buffer(Buffer& buffer) = 0;
    virtual void allocate_image(Image& image, AllocateMemory alloc = AllocateMemory::YES, void* user_data = nullptr) = 0;
    virtual void destroy_image(Image& b) = 0;
    virtual void allocate_view(const ImageView& view, void** out_allocation) = 0;
    virtual void allocate_sampler(Sampler& sampler) = 0;
    virtual void make_shader(Shader& shader) = 0;
    virtual bool compile_shader(const Shader& shader) = 0;
    virtual void destroy_shader(Shader& shader) = 0;
    virtual bool compile_layout(DescriptorLayout& layout) = 0;
    virtual bool compile_layout(PipelineLayout& layout) = 0;
    virtual void make_pipeline(Pipeline& pipeline) = 0;
    virtual void destroy_pipeline(Pipeline& pipeline) = 0;
    virtual bool compile_pipeline(const Pipeline& pipeline) = 0;
    virtual Sync* make_sync(const SyncCreateInfo& info) = 0;
    virtual void destory_sync(Sync*) = 0;
    virtual Swapchain* make_swapchain() = 0;
    virtual void destroy_swapchain(Swapchain* swapchain) = 0;
    virtual SubmitQueue* get_queue(QueueType type) = 0;

    virtual ImageView::Metadata get_md(const ImageView& view) = 0;

    virtual size_t get_indirect_indexed_command_size() const = 0;
    virtual void make_indirect_indexed_command(void* out, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                               int32_t first_vertex, uint32_t first_instance) const = 0;

    // Gets requirements for a resource. Passing same reqs pointer multiple times accumulates requirements: max(size), max(alignment)
    virtual void get_memory_requirements(const Buffer& resource, RendererMemoryRequirements& reqs) = 0;
    // Gets requirements for a resource. Passing same reqs pointer multiple times accumulates requirements: max(size), max(alignment)
    virtual void get_memory_requirements(const Image& resource, RendererMemoryRequirements& reqs) = 0;
    // Allocates aliasable memory based on memory requiremets built from the set of resources that want to share the memory.
    // Returns null if the resources cannot be in the same memory (possibly due to memory heap not supporing all the resources).
    virtual void* allocate_aliasable_memory(const RendererMemoryRequirements& reqs) = 0;
    virtual void bind_aliasable_memory(Buffer& resource, void* memory, size_t offset) = 0;
    virtual void bind_aliasable_memory(Image& resource, void* memory, size_t offset) = 0;

    virtual void set_debug_name(Buffer& resource, std::string_view name) const = 0;
    virtual void set_debug_name(Image& resource, std::string_view name) const = 0;

    virtual QueryPool* make_query_pool(const QueryPoolCreateInfo& info) = 0;
    virtual void destroy_query_pool(QueryPool* pool) = 0;
    virtual void get_query_pool_results(QueryPool* pool, uint32_t query, uint32_t count, void* outdata) = 0;
    virtual size_t get_query_result_size(QueryType type) = 0;

    RendererBackendCaps caps{};
    RendererBackendLimits limits{};
};

} // namespace gfx
} // namespace eng
