#pragma once

#include <vector>
#include <array>
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

struct VkShaderMetadata
{
    VkShaderModule shader{};
};

struct VkPipelineLayoutMetadata
{
    static void init(PipelineLayout& a);
    static void destroy(PipelineLayout& a);
    static VkPipelineLayoutMetadata* get(const PipelineLayout& a)
    {
        return static_cast<VkPipelineLayoutMetadata*>(a.metadata);
    }
    std::vector<VkDescriptorSetLayout> dlayouts;
    VkPipelineLayout layout{};
};

struct VkPipelineMetadata
{
    static void init(const Pipeline& a);
    static void destroy(Pipeline& a);
    VkPipeline pipeline{};
};

struct VkDescriptorPoolMetadata
{
    static void init(DescriptorPool& a);
    static void destroy(DescriptorPool& a);
    static VkDescriptorPoolMetadata* get(const DescriptorPool& a)
    {
        return static_cast<VkDescriptorPoolMetadata*>(a.metadata);
    }
    VkDescriptorPool pool{};
};

struct VkDescriptorSetMetadata
{
    static VkDescriptorSetMetadata* get(const DescriptorSet& a)
    {
        return static_cast<VkDescriptorSetMetadata*>(a.metadata);
    }
    VkDescriptorSet set{};
};

struct VkBufferMetadata
{
    static void init(Buffer& a);
    static void destroy(Buffer& a);
    static VkBufferMetadata& get(Buffer& a);
    static const VkBufferMetadata& get(const Buffer& a);
    VkBuffer buffer{};
    VmaAllocation vmaa{};
    VkDeviceAddress bda{};
};

struct VkImageMetadata
{
    static void init(Image& a, VkImage img = {});
    static void destroy(Image& a, bool destroy_image = true);
    VkImage image{};
    VmaAllocation vmaa{};
};

struct VkImageViewMetadata
{
    static void init(ImageView& a);
    static void destroy(ImageView& a);
    VkImageView view{};
};

struct VkSamplerMetadata
{
    static void init(Sampler& a);
    static void destroy(Sampler& a);
    static VkSamplerMetadata& get(Sampler& a);
    static const VkSamplerMetadata& get(const Sampler& a);
    // static const VkSamplerMetadata& get(const Sampler& a);
    VkSampler sampler{};
};

struct VkSwapchainMetadata
{
    static void init(Swapchain& a);
    static void destroy(Swapchain& a);
    static VkSwapchainMetadata& get(Swapchain& a);
    static uint32_t acquire(Swapchain* a, uint64_t timeout, Sync* semaphore, Sync* fence);
    VkSwapchainKHR swapchain{};
};

class RendererBackendVulkan : public RendererBackend
{
  public:
    static RendererBackendVulkan* get_instance();

    RendererBackendVulkan() = default;
    RendererBackendVulkan(const RendererBackendVulkan&) = delete;
    RendererBackendVulkan& operator=(const RendererBackendVulkan&) = delete;
    ~RendererBackendVulkan() override = default;

    void init() final;
    void initialize_vulkan();

    Buffer make_buffer(const BufferDescriptor& info) final;
    Image make_image(const ImageDescriptor& info) final;
    void make_view(ImageView& view) final;
    Sampler make_sampler(const SamplerDescriptor& info) final;
    void make_shader(Shader& shader) final;
    bool compile_shader(const Shader& shader) final;
    bool compile_pplayout(PipelineLayout& layout) final;
    void make_pipeline(Pipeline& pipeline) final;
    bool compile_pipeline(const Pipeline& pipeline) final;
    Sync* make_sync(const SyncCreateInfo& info) final;
    Swapchain* make_swapchain() final;
    SubmitQueue* get_queue(QueueType type) final;
    DescriptorPool make_descpool(const DescriptorPoolCreateInfo& info) final;
    DescriptorSet allocate_set(DescriptorPool& pool, const PipelineLayout& playout, uint32_t dset_idx) final;

    // void bake_indirect_commands();
    // void build_transforms_buffer();

    // void build_blas();
    // void build_tlas();
    // void update_ddgi();

    // void destroy_buffer(Handle<Buffer> buffer);
    // void destroy_image(Handle<Image> image);
    // void destroy_view(Handle<ImageView> view);
    // uint32_t get_bindless(Handle<Buffer> buffer);
    // void update_resource(Handle<Buffer> dst);

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

    // std::vector<ecs::entity> blas_instances;

    // VkAccelerationStructureKHR tlas{};
    // Handle<Buffer> tlas_buffer;
    // Handle<Buffer> tlas_instance_buffer;
    // Handle<Buffer> tlas_scratch_buffer;

    // Handle<Buffer> tlas_mesh_offsets_buffer;
    // Handle<Buffer> tlas_transform_buffer;
    // Handle<Buffer> blas_mesh_offsets_buffer;
    // Handle<Buffer> triangle_geo_inst_id_buffer;
    // Handle<Buffer> mesh_instances_buffer;

    // DDGI ddgi;
    // gfx::VsmData vsm; // TODO: not sure if vsmdata should be in gfx and renderer.hpp
    // FFTOcean fftocean;

    // std::vector<Handle<Shader>> shaders_to_compile;
    // std::vector<Handle<Pipeline>> pipelines_to_compile;
    // std::vector<MeshletInstance> meshlets_to_instance;
};
} // namespace gfx

} // namespace eng
