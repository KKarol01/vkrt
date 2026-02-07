#pragma once

#include <vector>
#include <array>
#include <tuple>
#include <unordered_map>
#include <glm/mat4x3.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/handlemap.hpp>
#include <eng/common/handleflatset.hpp>
#include <eng/common/handlesparsevec.hpp>

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
        uint32_t frame_num;
        int32_t irradiance_probe_side;
        int32_t visibility_probe_side;
        uint32_t rays_per_probe;
        VkDeviceAddress debug_probe_offsets;
    };
    using GPUProbeOffsetsLayout = glm::vec3;

    // BoundingBox probe_dims;
    float probe_distance{ 0.4f };
    glm::uvec3 probe_counts;
    glm::vec3 probe_walk;
    glm::vec3 probe_start;
    int32_t irradiance_probe_side{ 6 };
    int32_t visibility_probe_side{ 14 };
    uint32_t rays_per_probe{ 64 };
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
        uint32_t gaussian;
        uint32_t h0;
        uint32_t ht;
        uint32_t dtx;
        uint32_t dtz;
        uint32_t dft;
        uint32_t disp;
        uint32_t grad;
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
    static void destroy(Pipeline& a);
    VkPipeline pipeline{};
};

struct BufferMetadataVk
{
    static void init(Buffer& a, std::optional<dont_alloc_tag> dont_alloc);
    static void init(const Buffer& a, VkBufferCreateInfo& info);
    static void destroy(Buffer& a);
    VkBuffer buffer{};
    VmaAllocation vma_alloc{};
    VkDeviceAddress device_address{};
    bool is_aliased{};
};

struct ImageMetadataVk
{
    static void init(Image& a, VkImage img = {}, std::optional<dont_alloc_tag> dont_alloc = {});
    static void init(Image& a, VkImageCreateInfo& info);
    static void destroy(Image& a, bool deallocate = true);
    VkImage image{};
    VmaAllocation vmaa{};
    std::unordered_map<ImageView, ImageViewMetadataVk> views;
    bool is_aliased{};
};

struct ImageViewMetadataVk
{
    static void init(const ImageView& view, void** out_allocation);
    static void destroy(ImageView& a);
    VkImageView view{};
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
    static uint32_t acquire(Swapchain* a, uint64_t timeout, Sync* semaphore, Sync* fence);
    VkSwapchainKHR swapchain{};
};

class RendererBackendVk : public IRendererBackend
{
  public:
    struct IndirectIndexedCommand
    {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t vertexOffset;
        uint32_t firstInstance;
    };

    static RendererBackendVk& get_instance();
    static VkDevice get_dev() { return get_instance().dev; }

    RendererBackendVk() = default;
    RendererBackendVk(const RendererBackendVk&) = delete;
    RendererBackendVk& operator=(const RendererBackendVk&) = delete;
    ~RendererBackendVk() override = default;

    void init() override;
    void initialize_vulkan();

    void allocate_buffer(Buffer& buffer, std::optional<dont_alloc_tag> dont_alloc) override;
    void destroy_buffer(Buffer& buffer) override;
    void allocate_image(Image& image, std::optional<dont_alloc_tag> dont_alloc) override;
    void destroy_image(Image& image) override;
    void allocate_view(const ImageView& view, void** out_allocation) override;
    Sampler make_sampler(const SamplerDescriptor& info) override;
    void make_shader(Shader& shader) override;
    bool compile_shader(const Shader& shader) override;
    bool compile_layout(DescriptorLayout& layout) override;
    bool compile_layout(PipelineLayout& layout) override;
    void make_pipeline(Pipeline& pipeline) override;
    bool compile_pipeline(const Pipeline& pipeline) override;
    Sync* make_sync(const SyncCreateInfo& info) override;
    void destory_sync(Sync* sync) override;
    Swapchain* make_swapchain() override;
    SubmitQueue* get_queue(QueueType type) override;

    ImageView::Metadata get_md(const ImageView& view) override;

    size_t get_indirect_indexed_command_size() const override;
    void make_indirect_indexed_command(void* out, uint32_t index_count, uint32_t instance_count, uint32_t first_index,
                                       int32_t first_vertex, uint32_t first_instance) const override;

    void get_memory_requirements(const Buffer& resource, RendererMemoryRequirements& reqs) override;
    void get_memory_requirements(const Image& resource, RendererMemoryRequirements& reqs) override;
    void* allocate_aliasable_memory(const RendererMemoryRequirements& reqs) override;
    void bind_aliasable_memory(Buffer& resource, void* memory, size_t offset) override;
    void bind_aliasable_memory(Image& resource, void* memory, size_t offset) override;

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    SubmitQueue* gq;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;
    // Flags<RenderFlags> flags;
    bool supports_raytracing{ false };

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;
};
} // namespace gfx

} // namespace eng
