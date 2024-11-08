#pragma once

#include <span>
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
enum class RendererFlags : u32 {
    DIRTY_MESH_INSTANCES = 0x1,
    DIRTY_GEOMETRY_BATCHES_BIT = 0x2,
    DIRTY_MESH_BLAS_BIT = 0x4,
    DIRTY_TLAS_BIT = 0x8,
    REFIT_TLAS_BIT = 0x10,
    DIRTY_TRANSFORMS_BIT = 0x20,
    RESIZE_SWAPCHAIN_BIT = 0x40,
    RESIZE_SCREEN_RECT_BIT = 0x80,
};

enum class GeometryFlags : u32 {};
enum class MeshBatchFlags { DIRTY_BLAS_BIT = 0x1 };

/* Used by mesh instance to index textures in the shader */
struct RenderMaterial {
    Handle<RenderTexture> color_texture;
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
    u32 vertex_offset{ 0 };
    u32 vertex_count{ 0 };
    u32 index_offset{ 0 };
    u32 index_count{ 0 };
};

/* subset of geometry's vertex and index buffers */
struct RenderMesh {
    Flags<MeshBatchFlags> flags;
    Handle<RenderGeometry> geometry;
    Handle<MeshMetadata> metadata;
    u32 vertex_offset{ 0 };
    u32 vertex_count{ 0 };
    u32 index_offset{ 0 };
    u32 index_count{ 0 };
};

/* render mesh with material and entity handle for ec system */
struct MeshInstance {
    Handle<MeshInstance> handle;
    Handle<Entity> entity;
    Handle<RenderMesh> mesh;
    Handle<MaterialBatch> material;
};

/* unpacked mesh instance for gpu consumption */
struct GPUMeshInstance {
    u32 vertex_offset{};
    u32 index_offset{};
    u32 color_texture_idx;
};

/* for each meshinstance struct that has raytracing flag set */
struct BLASInstance {
    Handle<BLASInstance> handle;
    Handle<MeshInstance> render_handle;
    Handle<RenderMesh> mesh_batch;
    VkAccelerationStructureKHR blas;
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
        f32 min_probe_distance;
        f32 max_probe_distance;
        f32 min_dist;
        f32 max_dist;
        f32 normal_bias;
        f32 max_probe_offset;
        u32 frame_num;
        s32 irradiance_probe_side;
        s32 visibility_probe_side;
        u32 rays_per_probe;
        VkDeviceAddress debug_probe_offsets;
    };
    using GPUProbeOffsetsLayout = glm::vec3;

    BoundingBox probe_dims;
    f32 probe_distance{ 0.4f };
    glm::uvec3 probe_counts;
    glm::vec3 probe_walk;
    glm::vec3 probe_start;
    s32 irradiance_probe_side{ 6 };
    s32 visibility_probe_side{ 14 };
    u32 rays_per_probe{ 64 };
    Buffer buffer;
    Buffer debug_probe_offsets_buffer;
    Image* radiance_texture{};
    Image* irradiance_texture{};
    Image* visibility_texture{};
    Image* probe_offsets_texture{};
    std::vector<Handle<Node>> debug_probes;
};

struct IndirectDrawCommandBufferHeader {
    u32 draw_count{};
    u32 geometry_instance_count{};
};

struct Fence {
    Fence() = default;
    Fence(VkDevice dev, bool signaled);
    Fence(Fence&& f) noexcept;
    Fence& operator=(Fence&& f) noexcept;
    ~Fence() noexcept;
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
        std::filesystem::path path;
        VkShaderStageFlagBits stage;
    };

    VkShaderModule get_shader(const std::filesystem::path& path);
    VkShaderStageFlagBits get_stage(std::filesystem::path path) const;
    VkShaderModule compile_shader(std::filesystem::path path);

    std::unordered_map<VkShaderModule, ShaderMetadata> metadatas;
};

struct DescriptorBinding {
    using Resource =
        std::variant<std::monostate, const Buffer*, const Image*, VkAccelerationStructureKHR*, const std::vector<Image>*>;
    DescriptorBinding() = default;
    DescriptorBinding(Resource res, u32 count, VkImageLayout layout, std::optional<VkSampler> sampler = {});
    DescriptorBinding(Resource res, u32 count, std::optional<VkSampler> sampler = {});
    DescriptorBinding(Resource res);
    VkDescriptorType get_vktype() const;
    VkImageLayout deduce_layout(const Resource& res, const std::optional<VkSampler>& sampler);
    Resource res{};
    VkImageLayout layout{ VK_IMAGE_LAYOUT_MAX_ENUM };
    std::optional<VkSampler> sampler{}; // if not set - storage image; if set, but sampler is nullptr - sampled image; if set and sampler is not nullptr - combined image sampler
    u32 count{ 1 };
};

struct DescriptorLayout {
    inline static constexpr u32 MAX_BINDINGS = 16;
    bool is_empty() const;
    std::array<DescriptorBinding, MAX_BINDINGS> bindings{};
    VkDescriptorSetLayout layout{};
    s32 variable_binding{ -1 };
};

struct PipelineLayout {
    inline static constexpr u32 MAX_SETS = 4;

    PipelineLayout() = default;
    PipelineLayout(std::array<DescriptorLayout, MAX_SETS> desc_layouts, u32 push_size = 128);

    std::array<DescriptorLayout, MAX_SETS> sets{};
    VkPipelineLayout layout{};
    u32 push_size{};
};

struct Pipeline {
    enum Type { None, Raster, Compute, RT };
    struct RasterizationSettings {
        u32 num_col_formats{ 1 };
        std::array<VkFormat, 4> col_formats{ { VK_FORMAT_R8G8B8A8_SRGB } };
        VkFormat dep_format{ VK_FORMAT_D16_UNORM };
        VkCullModeFlags culling{ VK_CULL_MODE_BACK_BIT };
        bool depth_test{ true };
        VkCompareOp depth_op{ VK_COMPARE_OP_LESS };
    };
    struct RaytracingSettings {
        u32 recursion_depth{ 1 };
        u32 group_count{};
        Buffer* sbt;
    };

    Pipeline() = default;
    Pipeline(const std::vector<VkShaderModule>& shaders, const PipelineLayout* layout,
             std::variant<std::monostate, RasterizationSettings, RaytracingSettings> settings = {});
    VkPipelineBindPoint get_bindpoint() const;

    const PipelineLayout* layout{};
    VkPipeline pipeline{};
    Type type{ None };
    union {
        RasterizationSettings rasterization_settings;
        RaytracingSettings raytracing_settings;
    };
};

struct DescriptorPool {
    DescriptorPool() = default;
    DescriptorPool(const PipelineLayout* layout, u32 max_sets);
    void allocate(const VkDescriptorSetLayout* layouts, VkDescriptorSet** sets, u32 count = 1, std::span<u32> variable_count = {});
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
    void push_constant(VkCommandBuffer cmd, u32 offset, u32 size, const void* value);
    const Pipeline* pipeline{};
    DescriptorPool* desc_pool{};
    std::array<VkDescriptorSet*, PipelineLayout::MAX_SETS> sets{};
};

template <size_t frames> struct Swapchain {
    Swapchain() = default;
    void create();
    u32 acquire(VkResult* res, u64 timeout = -1ull, VkSemaphore semaphore = {}, VkFence fence = {});
    VkSwapchainKHR swapchain{};
    std::array<Image, frames> images;
};

struct RenderPasses {
    RenderPass ddgi_radiance;
    RenderPass ddgi_irradiance;
    RenderPass ddgi_offsets;
    RenderPass default_lit;
};

template <size_t frames> struct FrameData {
    struct Data {
        Semaphore sem_swapchain{};
        Semaphore sem_rendering_finished{};
        Fence fen_rendering_finished{};
        CommandPool* cmdpool{};
        RenderPasses passes;
        Buffer* constants{};
        Image* depth_buffer{};
        DescriptorPool* descpool{};
    };
    Data& get() { return data[Engine::frame_num() % frames]; }
    std::array<Data, frames> data{};
};

struct QueueSubmission {
    std::span<VkCommandBuffer> cmds{};
    std::span<std::pair<Semaphore*, u32>> wait_sems{};
    std::span<std::pair<Semaphore*, u32>> signal_sems{};
    std::span<VkPipelineStageFlags2> wait_stages{};
    std::span<VkPipelineStageFlags2> signal_stages{};
};

struct QueueSubmit {
    void submit(VkQueue queue, QueueSubmission submissions, Fence* fence = {});
    void submit(VkQueue queue, std::span<QueueSubmission> submissions, Fence* fence = {});
};

class RendererVulkan : public Renderer {
  public:
    void init() final;

    static RendererVulkan* get() { return static_cast<RendererVulkan*>(Engine::renderer()); }

    void set_screen_rect(ScreenRect rect) final;

    void initialize_vulkan();
    // void create_swapchain();
    void initialize_imgui();
    void initialize_resources();

    void update() final;

    Handle<RenderTexture> batch_texture(const RenderTexture& batch) final;
    Handle<MaterialBatch> batch_material(const MaterialBatch& batch) final;
    Handle<RenderGeometry> batch_geometry(const GeometryDescriptor& batch) final;
    Handle<RenderMesh> batch_mesh(const MeshDescriptor& batch) final;
    Handle<MeshInstance> instance_mesh(const InstanceSettings& settings) final;
    Handle<BLASInstance> instance_blas(const BLASInstanceSettings& settings) final;
    void update_transform(Handle<MeshInstance> handle) final;

    void upload_model_textures();
    void upload_staged_models();
    void upload_instances();
    void upload_transforms();

    void build_blas();
    void build_tlas();
    void refit_tlas();
    void update_ddgi();

    Image* make_image(Image&& img);
    Buffer* make_buffer(Buffer&& buf);

    u32 get_total_vertices() const {
        return geometries.empty() ? 0u : geometries.back().vertex_offset + geometries.back().vertex_count;
    }
    u32 get_total_indices() const {
        return geometries.empty() ? 0u : geometries.back().index_offset + geometries.back().index_count;
    }
    u32 get_total_triangles() const { return get_total_indices() / 3u; }


    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;
    Flags<RendererFlags> flags;
    VkRect2D screen_rect{ 1280, 768 };
    SamplerStorage samplers;

    u32 gqi;
    VkQueue gq;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    HandleVector<RenderGeometry> geometries;
    HandleVector<GeometryMetadata> geometry_metadatas;
    HandleVector<RenderMesh> meshes;
    HandleVector<MeshMetadata> mesh_metadatas;
    HandleVector<Image> textures;
    std::vector<MeshInstance> mesh_instances;
    std::unordered_map<Handle<MeshInstance>, u32> mesh_instance_idxs;
    HandleVector<RenderMaterial> materials;
    std::vector<BLASInstance> blas_instances;
    u32 max_draw_count{};

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

    Swapchain<2> swapchain;
    FrameData<2> frame_data;
    ShaderStorage shaders;
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
    struct UpdatePosition {
        u32 idx;
        glm::mat4 transform;
    };

    std::vector<Vertex> upload_vertices;
    std::vector<u32> upload_indices;
    std::vector<UploadImage> upload_images;
    std::vector<Handle<MeshInstance>> update_positions;
};

// clang-format off
inline RendererVulkan& get_renderer() { return *static_cast<RendererVulkan*>(Engine::renderer()); }

CREATE_HANDLE_DISPATCHER(RenderGeometry) { return &get_renderer().geometries.at(h); }
CREATE_HANDLE_DISPATCHER(GeometryMetadata) { return &get_renderer().geometry_metadatas.at(h); }
CREATE_HANDLE_DISPATCHER(RenderMesh) { return &get_renderer().meshes.at(h); }
CREATE_HANDLE_DISPATCHER(MeshMetadata) { return &get_renderer().mesh_metadatas.at(h); }
// clang-format on