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

enum class ShaderModuleType {
    RT_BASIC_CLOSEST_HIT,
    RT_BASIC_SHADOW_HIT,
    RT_BASIC_MISS,
    RT_BASIC_SHADOW_MISS,
    RT_BASIC_RAYGEN,
    RT_BASIC_PROBE_IRRADIANCE_COMPUTE,
    RT_BASIC_PROBE_PROBE_OFFSET_COMPUTE,

    DEFAULT_UNLIT_VERTEX,
    DEFAULT_UNLIT_FRAGMENT,
};

enum class RenderPipelineType {
    DEFAULT_UNLIT,
    DDGI_PROBE_RAYCAST,
    DDGI_PROBE_UPDATE,
    DDGI_PROBE_OFFSET,
};

struct ShaderModuleWrapper {
    VkShaderModule module{};
    VkShaderStageFlagBits stage{};
};

struct RenderPipelineWrapper {
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    u32 rt_shader_group_count{ 0 };
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
        i32 irradiance_probe_side;
        i32 visibility_probe_side;
        u32 rays_per_probe;
        VkDeviceAddress debug_probe_offsets;
    };
    using GPUProbeOffsetsLayout = glm::vec3;

    BoundingBox probe_dims;
    f32 probe_distance{ 0.4f };
    glm::uvec3 probe_counts;
    glm::vec3 probe_walk;
    glm::vec3 probe_start;
    i32 irradiance_probe_side{ 6 };
    i32 visibility_probe_side{ 14 };
    u32 rays_per_probe{ 64 };
    Buffer buffer;
    Buffer debug_probe_offsets_buffer;
    Image radiance_texture;
    Image irradiance_texture;
    Image visibility_texture;
    Image probe_offsets_texture;
    std::vector<Handle<Node>> debug_probes;
};

struct IndirectDrawCommandBufferHeader {
    u32 draw_count{};
    u32 geometry_instance_count{};
};

struct RenderingPrimitives {
    VkSemaphore sem_swapchain_image{};
    VkSemaphore sem_rendering_finished{};
    VkSemaphore sem_gui_start{};
    VkSemaphore sem_copy_to_sw_img_done{};
    VkFence fen_rendering_finished{};
    CommandPool cmdpool{};
    VkDescriptorPool desc_pool{};
    Buffer constants;
};

class RendererVulkan : public Renderer {

  public:
    void init() final;

    static RendererVulkan* get() { return static_cast<RendererVulkan*>(Engine::renderer()); }

    void set_screen_rect(ScreenRect rect) final;

    void initialize_vulkan();
    void create_swapchain();
    void initialize_imgui();

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

    void compile_shaders();
    void build_pipelines();
    void build_sbt();
    void create_rt_output_image();
    void build_blas();
    void build_tlas();
    void refit_tlas();
    void initialize_ddgi();

    u32 get_resource_idx(int offset = 0) const { return (Engine::frame_num() + offset) % 2; }
    RenderingPrimitives& get_primitives() { return primitives[get_resource_idx()]; }

    u32 get_total_vertices() const {
        return geometries.empty() ? 0u : geometries.back().vertex_offset + geometries.back().vertex_count;
    }
    u32 get_total_indices() const {
        return geometries.empty() ? 0u : geometries.back().index_offset + geometries.back().index_count;
    }
    u32 get_total_triangles() const { return get_total_indices() / 3u; }

    VkSemaphore create_semaphore();
    void destroy_semaphore(VkSemaphore sem);

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;
    Flags<RendererFlags> flags;
    VkRect2D screen_rect{};
    std::unique_ptr<GpuStagingManager> staging;
    SamplerStorage samplers;

    QueueScheduler scheduler_gq;
    u32 gqi, pqi, tqi1;
    VkQueue gq, pq, tq1;
    VkSwapchainKHR swapchain{};
    Image swapchain_images[2]{};
    VkImageView imgui_views[2]{};
    VkFormat swapchain_format;
    Image output_images[2]{};
    Image depth_buffers[2]{};
    vks::PhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    vks::PhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    std::unordered_map<ShaderModuleType, ShaderModuleWrapper> shader_modules;
    std::unordered_map<RenderPipelineType, RenderPipelineWrapper> pipelines;
    std::vector<RenderPipelineLayout> layouts;
    DescriptorPoolAllocator descriptor_pool_allocator;
    VkDescriptorPool imgui_desc_pool;

    HandleVector<RenderGeometry> geometries;
    HandleVector<GeometryMetadata> geometry_metadatas;
    HandleVector<RenderMesh> meshes;
    HandleVector<MeshMetadata> mesh_metadatas;
    HandleVector<Image> images;
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
    Buffer sbt;
    Buffer tlas_mesh_offsets_buffer;
    Buffer tlas_transform_buffer;
    Buffer blas_mesh_offsets_buffer;
    Buffer triangle_geo_inst_id_buffer;
    Buffer mesh_instances_buffer;

    Image rt_image;
    Buffer global_buffer;
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

    RenderingPrimitives primitives[2];
};

// clang-format off
inline RendererVulkan& get_renderer() { return *static_cast<RendererVulkan*>(Engine::renderer()); }

CREATE_HANDLE_DISPATCHER(RenderGeometry) { return &get_renderer().geometries.at(h); }
CREATE_HANDLE_DISPATCHER(GeometryMetadata) { return &get_renderer().geometry_metadatas.at(h); }
CREATE_HANDLE_DISPATCHER(RenderMesh) { return &get_renderer().meshes.at(h); }
CREATE_HANDLE_DISPATCHER(MeshMetadata) { return &get_renderer().mesh_metadatas.at(h); }
// clang-format on