#pragma once

#include <vector>
#include <array>
#include <glm/mat4x3.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/renderer_vulkan_wrappers.hpp>
#include <eng/renderer/pipeline.hpp>
#include <eng/renderer/passes/rendergraph.hpp>
#include <eng/renderer/resources/resources.hpp>
#include <eng/common/hash.hpp>
#include <eng/common/handle.hpp>
#include <eng/common/handlemap.hpp>
#include <eng/common/handleflatset.hpp>

namespace gfx
{

/* Controls renderer's behavior */
enum class RenderFlags : uint32_t
{
    DIRTY_MESH_INSTANCES = 0x1,
    DIRTY_GEOMETRY_BATCHES_BIT = 0x2,
    DIRTY_BLAS_BIT = 0x4,
    DIRTY_TLAS_BIT = 0x8,
    DIRTY_TRANSFORMS_BIT = 0x10,
    RESIZE_SWAPCHAIN_BIT = 0x20,
    UPDATE_BINDLESS_SET = 0x40,
    PAUSE_RENDERING = 0x80,
    REBUILD_RENDER_GRAPH = 0x100,
    // RESIZE_SCREEN_RECT_BIT = 0x80,
};

// struct MeshInstance
//{
//     Handle<Mesh> mesh;
// };

struct MeshletInstance
{
    bool operator==(const MeshletInstance& o) const
    {
        return submesh_index == o.submesh_index && meshlet_index == o.meshlet_index;
    }
    ecs::Entity entity;
    uint32_t submesh_index{ ~0u }; // global submesh index
    uint32_t meshlet_index{ ~0u }; // global meshlet idx
};

struct GeometryBuffers
{
    Handle<Buffer> buf_vpos;          // positions
    Handle<Buffer> buf_vattrs;        // rest of attributes
    Handle<Buffer> buf_vidx;          // indices
    Handle<Buffer> buf_draw_cmds;     // draw commands
    Handle<Buffer> buf_draw_settings; // draw settings (for cullling)
    VkIndexType index_type{ VK_INDEX_TYPE_UINT16 };
    size_t num_vertices{};
    size_t num_indices{};
};

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

struct IndirectDrawCommandBufferHeader
{
    uint32_t draw_count{};
    uint32_t geometry_instance_count{};
};

struct Swapchain
{
    void create(VkDevice dev, uint32_t image_count, uint32_t width, uint32_t height);
    uint32_t acquire(VkResult* res, uint64_t timeout = -1ull, VkSemaphore semaphore = {}, VkFence fence = {});
    Image& get_current_image();
    VkImageView& get_current_view();
    VkSwapchainKHR swapchain{};
    std::vector<Image> images;
    std::vector<VkImageView> views;
    uint32_t current_index{ 0ul };
};

struct GBuffer
{
    Handle<Image> color_image{};
    Handle<Image> view_space_positions_image{};
    Handle<Image> view_space_normals_image{};
    Handle<Image> depth_buffer_image{};
    Handle<Image> ambient_occlusion_image{};
};

class CommandPool;

struct FrameData
{
    CommandPool* cmdpool{};
    VkSemaphore sem_swapchain{};
    VkSemaphore sem_rendering_finished{};
    VkFence fen_rendering_finished{};
    Handle<Buffer> constants{};
    Handle<Buffer> transform_buffers{};
    GBuffer gbuffer{};
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

struct ShaderMetadata
{
    VkShaderModule shader{};
};

struct PipelineMetadata
{
};

class SubmitQueue;
class StagingBuffer;
class BindlessPool;

class RendererVulkan : public Renderer
{
  public:
    static RendererVulkan* get_instance();

    RendererVulkan() = default;
    RendererVulkan(const RendererVulkan&) = delete;
    RendererVulkan& operator=(const RendererVulkan&) = delete;
    ~RendererVulkan() override = default;

    void init() final;
    void initialize_vulkan();
    void initialize_imgui();
    void initialize_resources();
    void initialize_mesh_passes();
    void initialize_materials();
    void create_window_sized_resources();
    void build_render_graph();

    void update() final;
    void on_window_resize() final;

    Handle<Image> batch_image(const ImageDescriptor& desc) final;
    Handle<Texture> batch_texture(const TextureDescriptor& batch) final;
    Handle<Material> batch_material(const MaterialDescriptor& desc) final;
    Handle<Geometry> batch_geometry(const GeometryDescriptor& batch) final;
    static void meshletize_geometry(const GeometryDescriptor& batch, std::vector<gfx::Vertex>& out_vertices,
                                    std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets);
    Handle<Mesh> batch_mesh(const MeshDescriptor& batch) final;
    Handle<Mesh> instance_mesh(const InstanceSettings& settings) final;
    void instance_blas(const BLASInstanceSettings& settings) final;
    void update_transform(ecs::Entity entity) final;
    size_t get_imgui_texture_id(Handle<Image> handle, ImageFiltering filter, ImageAddressing addressing, uint32_t layer) final;
    Handle<Image> get_color_output_texture() const final;

    void compile_shaders();
    void compile_pipelines();
    void bake_indirect_commands();

    void build_blas();
    void build_tlas();
    void update_ddgi();

    Handle<Shader> make_shader(Shader::Stage stage, const std::filesystem::path& path);
    Handle<Pipeline> make_pipeline(const PipelineCreateInfo& info);
    Handle<ShaderEffect> make_shader_effect(const ShaderEffect& info);
    Handle<MeshPass> make_mesh_pass(const MeshPassCreateInfo& info);
    Handle<Buffer> make_buffer(const BufferCreateInfo& info);
    Handle<Image> make_image(const ImageCreateInfo& info);
    Handle<Texture> make_texture(Handle<Image> image, VkImageView view, VkImageLayout layout, VkSampler sampler);
    Handle<Texture> make_texture(Handle<Image> image, VkImageLayout layout, VkSampler sampler);

    void destroy_buffer(Handle<Buffer> buffer);
    void destroy_image(Handle<Image> image);
    void update_resource(Handle<Buffer> dst);

    FrameData& get_frame_data(uint32_t offset = 0);
    const FrameData& get_frame_data(uint32_t offset = 0) const;

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;
    Flags<RenderFlags> flags;
    bool supports_raytracing{ false };

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    SubmitQueue* submit_queue{};
    StagingBuffer* staging_buffer{};
    BindlessPool* bindless_pool{};
    // RenderGraph rendergraph;

    SamplerStorage samplers;
    HandleMap<Buffer> buffers;
    HandleMap<Image> images;
    HandleFlatSet<Texture> textures;
    HandleFlatSet<Geometry> geometries;
    HandleFlatSet<Material> materials;
    HandleFlatSet<Pipeline> pipelines;
    HandleFlatSet<ShaderEffect> shader_effects;
    std::unordered_map<std::filesystem::path, Shader> shaders;
    std::unordered_map<std::string, MeshPass> mesh_passes;
    std::vector<Meshlet> meshlets;
    std::vector<Mesh> meshes;
    std::vector<uint32_t> mesh_instances;

    GeometryBuffers geom_main_bufs;

    // std::vector<MeshletInstance> meshlet_instances;
    std::vector<ecs::Entity> blas_instances;

    VkAccelerationStructureKHR tlas{};
    Handle<Buffer> tlas_buffer;
    Handle<Buffer> tlas_instance_buffer;
    Handle<Buffer> tlas_scratch_buffer;

    Handle<Buffer> vertex_positions_buffer;
    Handle<Buffer> vertex_attributes_buffer;
    Handle<Buffer> index_buffer;
    Handle<Buffer> index16_buffer;

    Handle<Buffer> meshlets_bs_buf; // meshlets bounding spheres buffer - for culling
    Handle<Buffer> meshlets_mli_id_buf; // meshlet instance to meshlet id buffer; for example: ml_bs_buf[ml_mli_id_buf[gl_InstanceIndex]]
    Handle<Buffer> meshlets_ind_draw_buf;

    Handle<Buffer> tlas_mesh_offsets_buffer;
    Handle<Buffer> tlas_transform_buffer;
    Handle<Buffer> blas_mesh_offsets_buffer;
    Handle<Buffer> triangle_geo_inst_id_buffer;
    Handle<Buffer> mesh_instances_buffer;

    Swapchain swapchain;
    std::array<FrameData, 2> frame_datas{};

    Handle<Material> default_material;

    // DDGI ddgi;
    // gfx::VsmData vsm; // TODO: not sure if vsmdata should be in gfx and renderer.hpp
    // FFTOcean fftocean;

    std::vector<Handle<Shader>> shaders_to_compile;
    std::vector<Handle<Pipeline>> pipelines_to_compile;
};
} // namespace gfx

DEFINE_HANDLE_DISPATCHER(gfx::Buffer, { return &gfx::RendererVulkan::get_instance()->buffers.at(handle); });
DEFINE_HANDLE_DISPATCHER(gfx::Image, { return &gfx::RendererVulkan::get_instance()->images.at(handle); });
DEFINE_HANDLE_DISPATCHER(gfx::Geometry, { return &gfx::RendererVulkan::get_instance()->geometries.at(handle); });
DEFINE_HANDLE_DISPATCHER(gfx::Mesh, { return &gfx::RendererVulkan::get_instance()->meshes.at(*handle); });
DEFINE_HANDLE_DISPATCHER(gfx::Texture, { return &gfx::RendererVulkan::get_instance()->textures.at(handle); });
DEFINE_HANDLE_DISPATCHER(gfx::Material, { return &gfx::RendererVulkan::get_instance()->materials.at(*handle); });