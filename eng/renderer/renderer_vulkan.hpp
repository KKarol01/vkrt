#pragma once

#include <eng/common/handle_map.hpp>
#include <eng/renderer/common.hpp>
#include <glm/mat4x3.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/renderer_vulkan_wrappers.hpp>
#include <eng/renderer/passes/rendergraph.hpp>
#include <eng/renderer/pipeline.hpp>

namespace gfx {

/* Controls renderer's behavior */
enum class RenderFlags : uint32_t {
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

enum class GeometryFlags : uint32_t { DIRTY_BLAS_BIT = 0x1 };
enum class MeshFlags : uint32_t {};

struct GeometryMetadata {
    VkAccelerationStructureKHR blas{};
    Handle<Buffer> blas_buffer{};
};

struct MeshMetadata {};

/* position inside vertex and index buffer*/
struct Geometry {
    Flags<GeometryFlags> flags;
    Handle<GeometryMetadata> metadata;
    uint32_t vertex_offset{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t index_offset{ 0 };
    uint32_t index_count{ 0 };
};

struct Material {
    Handle<Texture> base_color_texture{ ~0u };
};

struct Texture {
    Handle<Image> image;
    VkImageView view{};
    VkImageLayout layout{};
    VkSampler sampler{};
};

/* subset of geometry's vertex and index buffers */
struct Mesh {
    // Flags<MeshFlags> flags;
    Handle<Geometry> geometry;
    Handle<Material> material;
    // Handle<MeshMetadata> metadata;
    //  Handle<gfx::BLAS> blas;
};

struct MeshInstance {
    components::Entity entity;
    Handle<Mesh> mesh;
};

struct DDGI {
    struct GPULayout {
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

    BoundingBox probe_dims;
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

struct IndirectDrawCommandBufferHeader {
    uint32_t draw_count{};
    uint32_t geometry_instance_count{};
};

struct Swapchain {
    void create(VkDevice dev, uint32_t image_count, uint32_t width, uint32_t height);
    uint32_t acquire(VkResult* res, uint64_t timeout = -1ull, VkSemaphore semaphore = {}, VkFence fence = {});
    Image& get_current_image();
    VkImageView& get_current_view();
    VkSwapchainKHR swapchain{};
    std::vector<Image> images;
    std::vector<VkImageView> views;
    uint32_t current_index{ 0ul };
};

struct GBuffer {
    Handle<Image> color_image{};
    Handle<Image> view_space_positions_image{};
    Handle<Image> view_space_normals_image{};
    Handle<Image> depth_buffer_image{};
    Handle<Image> ambient_occlusion_image{};
};

class CommandPool;

struct FrameData {
    CommandPool* cmdpool{};
    VkSemaphore sem_swapchain{};
    VkSemaphore sem_rendering_finished{};
    VkFence fen_rendering_finished{};
    Handle<Buffer> constants{};
    Handle<Buffer> transform_buffers{};
    GBuffer gbuffer{};
};

struct FFTOcean {
    struct FFTOceanSettings {
        float num_samples{ 512.0f };          // N
        float patch_size{ 3.7f };            // L
        glm::vec2 wind_dir{ 0.0f, -2.5f}; // w
        float phillips_const{ 0.59f };        // A
        float time_speed{ 0.24f };
        float disp_lambda{ -1.58f };
        float small_l{ 0.0001f };
    };
    struct FFTOceanPushConstants {
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

class SubmitQueue;
class StagingBuffer;
class BindlessDescriptorPool;

class RendererVulkan : public gfx::Renderer {
  public:
    static RendererVulkan* get_instance() { return static_cast<RendererVulkan*>(Engine::get().renderer); }

    RendererVulkan() = default;
    RendererVulkan(const RendererVulkan&) = delete;
    RendererVulkan& operator=(const RendererVulkan&) = delete;
    ~RendererVulkan() override = default;

    void init() final;
    void initialize_vulkan();
    void initialize_imgui();
    void initialize_resources();
    void create_window_sized_resources();
    void build_render_graph();

    void update() final;
    void on_window_resize() final;
    // void set_screen(ScreenRect screen) final;
    Handle<Image> batch_image(const ImageDescriptor& desc) final;
    Handle<Texture> batch_texture(const TextureDescriptor& batch) final;
    Handle<Material> batch_material(const MaterialDescriptor& desc) final;
    Handle<Geometry> batch_geometry(const GeometryDescriptor& batch) final;
    Handle<Mesh> batch_mesh(const MeshDescriptor& batch) final;
    void instance_mesh(const InstanceSettings& settings) final;
    void instance_blas(const BLASInstanceSettings& settings) final;
    void update_transform(components::Entity entity) final;
    size_t get_imgui_texture_id(Handle<Image> handle, ImageFiltering filter, ImageAddressing addressing, uint32_t layer) final;
    Handle<Image> get_color_output_texture() const final;
    Material get_material(Handle<Material> handle) const final;
    VsmData& get_vsm_data() final;

    void upload_model_images();
    void upload_staged_models();
    void bake_indirect_commands();
    void upload_transforms();

    void build_blas();
    void build_tlas();
    void update_ddgi();

    Handle<Buffer> make_buffer(const std::string& name, const VkBufferCreateInfo& vk_info, const VmaAllocationCreateInfo& vma_info);
    Handle<Buffer> make_buffer(const std::string& name, buffer_resizable_t resizable, const VkBufferCreateInfo& vk_info,
                               const VmaAllocationCreateInfo& vma_info);
    Handle<Image> make_image(const std::string& name, const VkImageCreateInfo& vk_info);
    Handle<Texture> make_texture(Handle<Image> image, VkImageView view, VkImageLayout layout, VkSampler sampler);
    Handle<Texture> make_texture(Handle<Image> image, VkImageLayout layout, VkSampler sampler);
    VkImageView make_image_view(Handle<Image> handle);
    VkImageView make_image_view(Handle<Image> handle, const VkImageViewCreateInfo& vk_info);

    static Buffer& get_buffer(Handle<Buffer> handle);
    static Image& get_image(Handle<Image> handle);
    void destroy_buffer(Handle<Buffer> buffer);
    void replace_buffer(Handle<Buffer> dst_buffer, Buffer&& src_buffer);
    uint32_t get_bindless_index(Handle<Buffer> handle);
    uint32_t get_bindless_index(Handle<Texture> handle);

    FrameData& get_frame_data(uint32_t offset = 0);
    const FrameData& get_frame_data(uint32_t offset = 0) const;

    uint32_t get_total_vertices() const { return total_vertices; }
    uint32_t get_total_indices() const { return total_indices; }
    uint32_t get_total_triangles() const { return total_indices / 3u; }

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
    BindlessDescriptorPool* bindless_pool{};
    RenderGraph rendergraph;
    PipelineCompiler pipelines{};

    HandleMap<Geometry> geometries;
    HandleMap<GeometryMetadata> geometry_metadatas;
    HandleMap<Mesh> meshes;
    HandleMap<MeshMetadata> mesh_metadatas;
    HandleMap<Material> materials;

    std::vector<MeshInstance> mesh_instances;
    // std::unordered_map<components::Entity, uint32_t> mesh_instance_idxs;
    std::vector<components::Entity> blas_instances;
    uint32_t max_draw_count{};
    uint32_t total_vertices{};
    uint32_t total_indices{};

    VkAccelerationStructureKHR tlas{};
    Handle<Buffer> tlas_buffer;
    Handle<Buffer> tlas_instance_buffer;
    Handle<Buffer> tlas_scratch_buffer;
    Handle<Buffer> vertex_positions_buffer;
    Handle<Buffer> vertex_attributes_buffer;
    Handle<Buffer> index_buffer;
    Handle<Buffer> indirect_draw_buffer;
    Handle<Buffer> mesh_instance_mesh_id_buffer;
    Handle<Buffer> tlas_mesh_offsets_buffer;
    Handle<Buffer> tlas_transform_buffer;
    Handle<Buffer> blas_mesh_offsets_buffer;
    Handle<Buffer> triangle_geo_inst_id_buffer;
    Handle<Buffer> mesh_instances_buffer;

    Swapchain swapchain;
    std::array<FrameData, 2> frame_datas{};

    SamplerStorage samplers;
    HandleMap<Image> images;
    HandleMap<Texture> textures;
    HandleMap<Buffer> buffers;

    DDGI ddgi;
    gfx::VsmData vsm; // TODO: not sure if vsmdata should be in gfx and renderer.hpp
    FFTOcean fftocean;

    struct UploadImage {
        Handle<Image> image_handle;
        std::vector<std::byte> rgba_data;
    };
    std::vector<gfx::Vertex> upload_vertices;
    std::vector<uint32_t> upload_indices;
    std::vector<UploadImage> upload_images;
    std::vector<components::Entity> update_positions;
};

} // namespace gfx
