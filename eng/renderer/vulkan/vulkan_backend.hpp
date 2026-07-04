#pragma once

#include <shared_mutex>
#include <vector>
#include <array>
#include <tuple>
#include <unordered_map>

#include <glm/mat4x3.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>

#include <eng/common/types.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/handleflatset.hpp>
#include <eng/renderer/backend.hpp>
#include <eng/renderer/vulkan/vulkan_structs.hpp>

namespace eng
{
namespace gfx
{
class SubmitQueue;
class StagingBuffer;
class BindlessPool;
struct Sync;
struct SyncCreateInfo;

struct DDGI
{
    struct GPULayout
    {
        glm::ivec2 radiance_tex_size;
        glm::ivec2 irradiance_tex_size;
        glm::ivec2 visibility_tex_size;
        glm::ivec2 probe_offset_tex_size;
        glm::vec3 probe_start;
        glm::uvec3 probe_counts;
        glm::vec3 probe_walk;
        float min_probe_distance;
        float max_probe_distance;
        float min_dist;
        float max_dist;
        float normal_bias;
        float max_probe_offset;
        u32 frame_num;
        i32 irradiance_probe_side;
        i32 visibility_probe_side;
        u32 rays_per_probe;
        VkDeviceAddress debug_probe_offsets;
    };
    using GPUProbeOffsetsLayout = glm::vec3;

    // BoundingBox probe_dims;
    float probe_distance{ 0.4f };
    glm::uvec3 probe_counts;
    glm::vec3 probe_walk;
    glm::vec3 probe_start;
    i32 irradiance_probe_side{ 6 };
    i32 visibility_probe_side{ 14 };
    u32 rays_per_probe{ 64 };
    Handle<Buffer> buffer;
    Handle<Buffer> debug_probe_offsets_buffer;
    Handle<Image> radiance_texture;
    Handle<Image> irradiance_texture;
    Handle<Image> visibility_texture;
    Handle<Image> probe_offsets_texture;
};

struct FFTOcean
{
    struct FFTOceanSettings
    {
        float num_samples{ 512.0f };       // N
        float patch_size{ 3.7f };          // L
        glm::vec2 wind_dir{ 0.0f, -2.5f }; // w
        float phillips_const{ 0.59f };     // A
        float time_speed{ 0.24f };
        float disp_lambda{ -1.58f };
        float small_l{ 0.0001f };
    };
    struct FFTOceanPushConstants
    {
        FFTOceanSettings settings;
        u32 gaussian;
        u32 h0;
        u32 ht;
        u32 dtx;
        u32 dtz;
        u32 dft;
        u32 disp;
        u32 grad;
        float time;
    };
    bool recalc_state_0{ true };
    FFTOceanSettings settings;
    FFTOceanPushConstants pc;
    Handle<Image> gaussian_distribution_image;
    Handle<Image> h0_spectrum;
    Handle<Image> ht_spectrum;
    Handle<Image> dtx_spectrum;
    Handle<Image> dtz_spectrum;
    Handle<Image> dft_pingpong;
    Handle<Image> displacement;
    Handle<Image> gradient;
};

struct ShaderMetadataVk
{
    VkShaderModule shader{};
};

struct DescriptorLayoutMetadataVk
{
    static void init(DescriptorLayout& a);
    static void destroy(DescriptorLayout& a);
    VkDescriptorSetLayout layout{};
};

struct PipelineLayoutMetadataVk
{
    static void init(PipelineLayout& a);
    static void destroy(PipelineLayout& a);
    VkPipelineLayout layout{};
};

struct PipelineMetadataVk
{
    static void init(const Pipeline& a);
    VkPipeline pipeline{};
};

struct BufferMetadataVk
{
    VkBuffer buffer{};
    VmaAllocation vma_alloc{};
    VkDeviceAddress device_address{};
    bool is_aliased{};
    StackString<64> debug_name;
};

struct ImageViewMetadataVk
{
    hash_t view_hash{};
    VkImageView view{};
};

struct ImageMetadataVk
{
    static void init(Image& a, AllocateMemory allocate = AllocateMemory::YES, void* vkimage = nullptr);
    static void init(Image& a, VkImageCreateInfo& info);
    static void destroy(Image& a);
    VkImage image{};
    VmaAllocation vmaa{};
    bool is_aliased{};
    StackString<64> debug_name;
    std::vector<ImageViewMetadataVk*> views;
};

struct SamplerMetadataVk
{
    static void init(Sampler& a);
    static void destroy(Sampler& a);
    VkSampler sampler{};
};

struct SwapchainMetadataVk
{
    static void init(Swapchain& a);
    static void destroy(Swapchain& a);
    static SwapchainMetadataVk& get(Swapchain& a);
    static u32 acquire(Swapchain* a, u64 timeout, Sync* semaphore, Sync* fence);
    VkSwapchainKHR swapchain{};
};

struct QueryPoolMetadataVk
{
    VkQueryPool vkpool{};
};

struct GeometryMetadataVk
{
    VkAccelerationStructureKHR blas{};
};

struct AsRequirementsMetadataVk
{
    struct BlasData
    {
    };
    struct TlasData
    {
        std::vector<VkAccelerationStructureInstanceKHR> instances;
    };
    BlasData blas;
    TlasData tlas;
};

class RendererBackendVk : public IRendererBackend
{
  public:
    struct IndirectIndexedCommand
    {
        u32 indexCount;
        u32 instanceCount;
        u32 firstIndex;
        i32 vertexOffset;
        u32 firstInstance;
    };

    static RendererBackendVk& get_instance();
    static VkDevice get_dev() { return get_instance().dev; }

    RendererBackendVk() = default;
    RendererBackendVk(const RendererBackendVk&) = delete;
    RendererBackendVk& operator=(const RendererBackendVk&) = delete;
    ~RendererBackendVk() override = default;

    void init() override;

    void allocate_buffer(Buffer& buffer, AllocateMemory allocate = AllocateMemory::YES) override;
    void destroy_buffer(Buffer& buffer) override;
    void allocate_image(Image& image, AllocateMemory allocate = AllocateMemory::YES, void* user_data = nullptr) override;
    void destroy_image(Image& image) override;
    void allocate_view(ImageView& view, void*& out_md) override;
	void destroy_view(void*& md) override;
    void allocate_sampler(Sampler& sampler) override;
    void make_shader(Shader& shader) override;
    bool compile_shader(const Shader& shader, std::span<const std::byte> bytecode) override;
    void destroy_shader(Shader& shader) override;
    bool compile_layout(DescriptorLayout& layout) override;
    bool compile_layout(PipelineLayout& layout) override;
    void make_pipeline(Pipeline& pipeline) override;
    void destroy_pipeline(Pipeline& pipeline) override;
    bool compile_pipeline(const Pipeline& pipeline) override;
    Sync* make_sync(const SyncCreateInfo& info) override;
    void destory_sync(Sync* sync) override;
    Swapchain* make_swapchain() override;
    void destroy_swapchain(Swapchain* swapchain) override;
    SubmitQueue* get_queue(QueueType type) override;
    void make_blas(Geometry& geom, ASRequirements& reqs, ICommandBuffer* cmd, Buffer* as_buffer, size_t as_offset,
                   Buffer* scratch_buffer, size_t scratch_offset) override;
    TopAccelerationStructure make_tlas(std::span<const Geometry*> geoms, std::span<const glm::mat3x4> transforms,
                                       std::span<const u32> instance_ids, ASRequirements& reqs, ICommandBuffer* cmd,
                                       Buffer* tlas_buffer, size_t tlas_offset, Buffer* scratch_buffer,
                                       size_t scratch_offset, Buffer* instances_buffer, size_t instances_offset) override;

    size_t get_indirect_indexed_command_size() const override;
    void make_indirect_indexed_command(void* out, u32 index_count, u32 instance_count, u32 first_index,
                                       i32 first_vertex, u32 first_instance) const override;

    void get_memory_requirements(const Buffer& resource, RendererMemoryRequirements& reqs) override;
    void get_memory_requirements(const Image& resource, RendererMemoryRequirements& reqs) override;
    void* allocate_aliasable_memory(const RendererMemoryRequirements& reqs) override;
    void bind_aliasable_memory(Buffer& resource, void* memory, size_t offset) override;
    void bind_aliasable_memory(Image& resource, void* memory, size_t offset) override;

    std::string_view get_debug_name(const Buffer& resource) const override;
    std::string_view get_debug_name(const Image& resource) const override;
    void set_debug_name(Buffer& resource, std::string_view name) const override;
    void set_debug_name(Image& resource, std::string_view name) const override;

    QueryPool* make_query_pool(const QueryPoolCreateInfo& info) override;
    void destroy_query_pool(QueryPool* pool) override;
    void get_query_pool_results(QueryPool* pool, u32 query, u32 count, void* outdata) override;
    size_t get_query_result_size(QueryType type) override;

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    SubmitQueue* gq;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;
    // Flags<RenderFlags> flags;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;
    VkPhysicalDeviceLimits pdev_limits{};
};
} // namespace gfx
} // namespace eng
