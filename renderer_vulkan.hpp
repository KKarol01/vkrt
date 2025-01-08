#pragma once

#include <span>
#include <filesystem>
#include <bitset>
#include <unordered_set>
#include <latch>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <glm/mat4x3.hpp>
#include "renderer.hpp"
#include "vulkan_structs.hpp"
#include "handle_vec.hpp"
#include "gpu_staging_manager.hpp"
#include "renderer_vulkan_wrappers.hpp"
#include "engine.hpp"

#ifndef NDEBUG
#define VK_CHECK(func)                                                                                                 \
    if(const auto res = func; res != VK_SUCCESS) { ENG_WARN("{}", #func); }
#else
#define VK_CHECK(func) func
#endif

/* Controls renderer's behavior */
enum class RendererFlags : uint32_t {
    DIRTY_MESH_INSTANCES = 0x1,
    DIRTY_GEOMETRY_BATCHES_BIT = 0x2,
    DIRTY_MESH_BLAS_BIT = 0x4,
    DIRTY_TLAS_BIT = 0x8,
    REFIT_TLAS_BIT = 0x10,
    DIRTY_TRANSFORMS_BIT = 0x20,
    RESIZE_SWAPCHAIN_BIT = 0x40,
    RESIZE_SCREEN_RECT_BIT = 0x80,
};

enum class GeometryFlags : uint32_t {};
enum class MeshBatchFlags { DIRTY_BLAS_BIT = 0x1 };

/* Used by mesh instance to index textures in the shader */
struct RenderMaterial {
    Handle<Image> color_texture;
    Handle<Image> normal_texture;
    Handle<Image> metallic_roughness_texture;
};

struct GeometryMetadata {};

struct MeshMetadata {
    VkAccelerationStructureKHR blas{};
    Buffer blas_buffer{};
};

/* position inside vertex and index buffer*/
struct RenderGeometry {
    Flags<GeometryFlags> flags;
    Handle<GeometryMetadata> metadata;
    uint32_t vertex_offset{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t index_offset{ 0 };
    uint32_t index_count{ 0 };
};

/* subset of geometry's vertex and index buffers */
struct RenderMesh {
    Flags<MeshBatchFlags> flags;
    Handle<RenderGeometry> geometry;
    Handle<MeshMetadata> metadata;
};

/* render mesh with material and entity handle for ec system */
// struct RenderInstance {
//     Handle<RenderInstance> handle;
//     Handle<Entity> entity;
//     Handle<RenderMesh> mesh;
//     Handle<RenderMaterial> material;
// };

/* unpacked mesh instance for gpu consumption */
struct GPUMeshInstance {
    uint32_t vertex_offset{};
    uint32_t index_offset{};
    uint32_t color_texture_idx;
    uint32_t normal_texture_idx;
    uint32_t metallic_roughness_idx;
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
    Buffer buffer;
    Buffer debug_probe_offsets_buffer;
    Image* radiance_texture{};
    Image* irradiance_texture{};
    Image* visibility_texture{};
    Image* probe_offsets_texture{};
    std::vector<Handle<Node>> debug_probes;
};

struct IndirectDrawCommandBufferHeader {
    uint32_t draw_count{};
    uint32_t geometry_instance_count{};
};

struct Fence {
    Fence() = default;
    Fence(VkDevice dev, bool signaled);
    Fence(Fence&& f) noexcept;
    Fence& operator=(Fence&& f) noexcept;
    ~Fence() noexcept;
    VkResult wait(uint32_t timeout = ~0u);
    VkFence fence{};
};

struct Semaphore {
    Semaphore() = default;
    Semaphore(VkDevice dev, bool timeline);
    Semaphore(Semaphore&&) noexcept;
    Semaphore& operator=(Semaphore&&) noexcept;
    ~Semaphore() noexcept;
    VkSemaphore semaphore{};
};

struct ShaderStorage {
    struct ShaderMetadata {
        VkShaderModule shader{};
        VkShaderStageFlagBits stage;
    };

    void precompile_shaders(std::vector<std::filesystem::path> paths);
    VkShaderModule get_shader(std::filesystem::path path);
    VkShaderStageFlagBits get_stage(std::filesystem::path path) const;
    VkShaderModule compile_shader(std::filesystem::path path);
    // TODO: maybe this should be global tool
    void canonize_path(std::filesystem::path& p);

    std::unordered_map<std::filesystem::path, ShaderMetadata> metadatas;
};

struct DescriptorBinding {
    using Resource =
        std::variant<std::monostate, const Buffer*, const Image*, VkAccelerationStructureKHR*, const std::vector<Image>*>;
    DescriptorBinding() = default;
    DescriptorBinding(Resource res, uint32_t count, VkImageLayout layout, std::optional<VkSampler> sampler = {});
    DescriptorBinding(Resource res, uint32_t count, std::optional<VkSampler> sampler = {});
    DescriptorBinding(Resource res);
    VkDescriptorType get_vktype() const;
    VkImageLayout deduce_layout(const Resource& res, const std::optional<VkSampler>& sampler);
    Resource res{};
    VkImageLayout layout{ VK_IMAGE_LAYOUT_MAX_ENUM };
    std::optional<VkSampler> sampler{}; // if not set - storage image; if set, but sampler is nullptr - sampled image; if set and sampler is not nullptr - combined image sampler
    uint32_t count{ 1 };
};

struct DescriptorLayout {
    inline static constexpr uint32_t MAX_BINDINGS = 16;
    bool is_empty() const;
    std::array<DescriptorBinding, MAX_BINDINGS> bindings{};
    VkDescriptorSetLayout layout{};
    int32_t variable_binding{ -1 };
};

struct PipelineLayout {
    inline static constexpr uint32_t MAX_SETS = 4;

    PipelineLayout() = default;
    PipelineLayout(std::array<DescriptorLayout, MAX_SETS> desc_layouts, uint32_t push_size = 128);

    std::array<DescriptorLayout, MAX_SETS> layouts{};
    VkPipelineLayout layout{};
    uint32_t push_size{};
};

struct Pipeline {
    enum Type { None, Raster, Compute, RT };
    struct RasterizationSettings {
        uint32_t num_col_formats{ 1 };
        std::array<VkFormat, 4> col_formats{ { VK_FORMAT_R8G8B8A8_SRGB } };
        VkFormat dep_format{ VK_FORMAT_D16_UNORM };
        VkCullModeFlags culling{ VK_CULL_MODE_BACK_BIT };
        bool depth_test{ true };
        VkCompareOp depth_op{ VK_COMPARE_OP_LESS };
        uint32_t num_vertex_bindings{};
        uint32_t num_vertex_attribs{};
        std::array<VkVertexInputBindingDescription, 8> vertex_bindings{};
        std::array<VkVertexInputAttributeDescription, 8> vertex_attribs{};
    };
    struct RaytracingSettings {
        uint32_t recursion_depth{ 1 };
        uint32_t group_count{};
        Buffer* sbt;
    };

    Pipeline() = default;
    Pipeline(const std::vector<std::filesystem::path>& shaders, const PipelineLayout* layout,
             std::variant<std::monostate, RasterizationSettings, RaytracingSettings> settings = {});

    const PipelineLayout* layout{};
    VkPipeline pipeline{};
    VkPipelineBindPoint bind_point{};
    union {
        RasterizationSettings rasterization_settings;
        RaytracingSettings raytracing_settings;
    };
};

struct DescriptorPool {
    DescriptorPool() = default;
    DescriptorPool(const PipelineLayout* layout, uint32_t max_sets);
    void allocate(const VkDescriptorSetLayout* layouts, VkDescriptorSet** sets, uint32_t count = 1,
                  std::span<uint32_t> variable_count = {});
    void reset();
    VkDescriptorPool pool{};
    std::deque<VkDescriptorSet> sets;
};

struct RenderPass {
    RenderPass() = default;
    // TODO: make actual pipeline with layout here
    RenderPass(const Pipeline* pipeline, DescriptorPool* desc_pool);
    void bind(VkCommandBuffer cmd);
    void bind_desc_sets(VkCommandBuffer cmd);
    void update_desc_sets();
    void push_constant(VkCommandBuffer cmd, uint32_t offset, uint32_t size, const void* value);
    const Pipeline* pipeline{};
    DescriptorPool* desc_pool{};
    std::array<VkDescriptorSet*, PipelineLayout::MAX_SETS> sets{};
};

struct Swapchain {
    void create(uint32_t image_count, uint32_t width, uint32_t height);
    uint32_t acquire(VkResult* res, uint64_t timeout = -1ull, VkSemaphore semaphore = {}, VkFence fence = {});
    VkSwapchainKHR swapchain{};
    std::vector<Image> images;
};

struct RenderPasses {
    RenderPass ddgi_radiance;
    RenderPass ddgi_irradiance;
    RenderPass ddgi_offsets;
    RenderPass default_lit;
    RenderPass rect_depth_buffer;
    RenderPass rect_bilateral_filter;
};

struct GBuffer {
    Image* color_image{};
    Image* view_space_positions_image{};
    Image* view_space_normals_image{};
    Image* depth_buffer_image{};
    Image* ambient_occlusion_image{};
};

struct FrameData {
    Semaphore sem_swapchain{};
    Semaphore sem_rendering_finished{};
    Fence fen_rendering_finished{};
    CommandPool* cmdpool{};
    RenderPasses passes{};
    Buffer* constants{};
    GBuffer gbuffer{};
    DescriptorPool* descpool{};
};

struct QueueCmdSubmission : public VkCommandBufferSubmitInfo {
    QueueCmdSubmission(VkCommandBuffer cmd)
        : VkCommandBufferSubmitInfo(Vks(VkCommandBufferSubmitInfo{ .commandBuffer = cmd })) {}
};

struct QueueSemaphoreSubmission : public VkSemaphoreSubmitInfo {
    QueueSemaphoreSubmission(VkPipelineStageFlags2 stage, Semaphore& sem, uint32_t value = 0)
        : VkSemaphoreSubmitInfo(Vks(VkSemaphoreSubmitInfo{ .semaphore = sem.semaphore, .value = value, .stageMask = stage })) {}
};

struct QueueSubmission {
    std::vector<QueueCmdSubmission> cmds{};
    std::vector<QueueSemaphoreSubmission> wait_sems{};
    std::vector<QueueSemaphoreSubmission> signal_sems{};
};

struct Queue {
    void submit(const QueueSubmission& submissions, Fence* fence = {});
    void submit(std::span<const QueueSubmission> submissions, Fence* fence = {});
    void submit(VkCommandBuffer cmd, Fence* fence = {});
    void wait_idle();
    VkQueue queue{};
    uint32_t idx{ ~0u };
};

class RendererVulkan : public Renderer {
  public:
    void init() final;

    void initialize_vulkan();
    void initialize_imgui();
    void initialize_resources();

    void update() final;

    void on_window_resize() final;
    void set_screen(ScreenRect screen) final;
    Handle<Image> batch_texture(const ImageDescriptor& desc) final;
    Handle<RenderMaterial> batch_material(const MaterialDescriptor& desc) final;
    Handle<RenderGeometry> batch_geometry(const GeometryDescriptor& batch) final;
    Handle<RenderMesh> batch_mesh(const MeshDescriptor& batch) final;
    void instance_mesh(const InstanceSettings& settings) final;
    void instance_blas(const BLASInstanceSettings& settings) final;
    void update_transform(components::Entity entity) final;

    void upload_model_textures();
    void upload_staged_models();
    void upload_instances();
    void upload_transforms();

    void build_blas();
    void build_tlas();
    void update_ddgi();

    Image* make_image(Image&& img);
    Buffer* make_buffer(Buffer&& buf);
    FrameData& get_frame_data(uint32_t offset = 0);

    uint32_t get_total_vertices() const {
        return geometries.empty() ? 0u : geometries.back().vertex_offset + geometries.back().vertex_count;
    }
    uint32_t get_total_indices() const {
        return geometries.empty() ? 0u : geometries.back().index_offset + geometries.back().index_count;
    }
    uint32_t get_total_triangles() const { return get_total_indices() / 3u; }

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;
    Flags<RendererFlags> flags;
    SamplerStorage samplers;
    ScreenRect screen_rect;

    Queue gq;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    HandleVector<RenderGeometry> geometries;
    HandleVector<GeometryMetadata> geometry_metadatas;
    HandleVector<RenderMesh> meshes;
    HandleVector<MeshMetadata> mesh_metadatas;
    HandleVector<Image> textures;
    HandleVector<RenderMaterial> materials;
    std::vector<components::Entity> mesh_instances;
    std::unordered_map<components::Entity, uint32_t> mesh_instance_idxs;
    std::vector<components::Entity> blas_instances;
    uint32_t max_draw_count{};

    VkAccelerationStructureKHR tlas{};
    Buffer tlas_buffer;
    Buffer tlas_instance_buffer;
    Buffer tlas_scratch_buffer;
    Buffer vertex_buffer, index_buffer;
    Buffer indirect_draw_buffer;
    Buffer* mesh_instance_transform_buffers[2]{};
    Buffer mesh_instance_mesh_id_buffer;
    Buffer tlas_mesh_offsets_buffer;
    Buffer tlas_transform_buffer;
    Buffer blas_mesh_offsets_buffer;
    Buffer triangle_geo_inst_id_buffer;
    Buffer mesh_instances_buffer;

    Swapchain swapchain;
    std::array<FrameData, 2> frame_datas{};
    ShaderStorage shader_storage;
    std::deque<PipelineLayout> playouts;
    std::deque<Pipeline> pipelines;
    std::deque<DescriptorPool> descpools;
    std::deque<CommandPool> cmdpools;
    std::deque<Image> images;
    std::deque<Buffer> buffers;

    DDGI ddgi;

    struct UploadImage {
        Handle<Image> image_handle;
        std::vector<std::byte> rgba_data;
    };

    std::vector<Vertex> upload_vertices;
    std::vector<uint32_t> upload_indices;
    std::vector<UploadImage> upload_images;
    std::vector<components::Entity> update_positions;
};

// clang-format off
inline RendererVulkan& get_renderer() { return *static_cast<RendererVulkan*>(Engine::get().renderer); }
// clang-format on
