#pragma once

#include <span>
#include <filesystem>
#include <bitset>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <forward_list>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <glm/mat4x3.hpp>
#include "renderer.hpp"
#include "vulkan_structs.hpp"
#include "handle_vec.hpp"
#include "gpu_staging_manager.hpp"
#include "renderer_vulkan_wrappers.hpp"
#include "engine.hpp"
#include "common/callback.hpp"

#ifndef NDEBUG
#define VK_CHECK(func)                                                                                                 \
    if(const auto res = func; res != VK_SUCCESS) { ENG_WARN("{}", #func); }
#else
#define VK_CHECK(func) func
#endif

/* Controls renderer's behavior */
enum class RenderFlags : uint32_t {
    DIRTY_MESH_INSTANCES = 0x1,
    DIRTY_GEOMETRY_BATCHES_BIT = 0x2,
    DIRTY_BLAS_BIT = 0x4,
    DIRTY_TLAS_BIT = 0x8,
    DIRTY_TRANSFORMS_BIT = 0x10,
    RESIZE_SWAPCHAIN_BIT = 0x20,
    UPDATE_BINDLESS_SET = 0x40,
    // RESIZE_SCREEN_RECT_BIT = 0x80,
};

enum class GeometryFlags : uint32_t { DIRTY_BLAS_BIT = 0x1 };
enum class RenderMeshFlags : uint32_t {};

/* Used by mesh instance to index textures in the shader */
struct RenderMaterial {
    Handle<Image> color_texture;
    Handle<Image> normal_texture;
    Handle<Image> metallic_roughness_texture;
};

struct GeometryMetadata {
    VkAccelerationStructureKHR blas{};
    Handle<Buffer> blas_buffer{};
};

struct MeshMetadata {};

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
    Flags<RenderMeshFlags> flags;
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

struct Fence {
    Fence() = default;
    Fence(VkDevice dev, bool signaled);
    Fence(Fence&& f) noexcept;
    Fence& operator=(Fence&& f) noexcept;
    ~Fence() noexcept;
    VkResult wait(uint64_t timeout = ~0ull);
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

struct Swapchain {
    void create(uint32_t image_count, uint32_t width, uint32_t height);
    uint32_t acquire(VkResult* res, uint64_t timeout = -1ull, VkSemaphore semaphore = {}, VkFence fence = {});
    VkSwapchainKHR swapchain{};
    std::vector<Image> images;
};

struct GBuffer {
    Handle<Image> color_image{};
    Handle<Image> view_space_positions_image{};
    Handle<Image> view_space_normals_image{};
    Handle<Image> depth_buffer_image{};
    Handle<Image> ambient_occlusion_image{};
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
    void submit_wait(VkCommandBuffer cmd);
    void wait_idle();
    VkQueue queue{};
    uint32_t idx{ ~0u };
};

struct PipelineLayout {
    VkPipelineLayout layout{};
    VkDescriptorSetLayout descriptor_layout{};
};

struct Pipeline {
    VkPipeline pipeline{};
};

namespace rendergraph {

enum class AccessType { NONE_BIT = 0x0, READ_BIT = 0x1, WRITE_BIT = 0x2, READ_WRITE_BIT = 0x3 };
enum class ResourceType {
    STORAGE_IMAGE = 0x1,
    COMBINED_IMAGE = 0x2,
    COLOR_ATTACHMENT = 0x4,
    ANY_IMAGE = 0x8 - 1,
    STORAGE_BUFFER = 0x8,
    ACCELERATION_STRUCTURE = 0x10,
};
enum class ResourceFlags : uint32_t { FROM_UNDEFINED_LAYOUT_BIT = 0x1, SWAPCHAIN_IMAGE_BIT = 0x2 };

ENABLE_FLAGS_OPERATORS(ResourceFlags)

struct RasterizationSettings {
    bool operator==(const RasterizationSettings& o) const {
        return num_col_formats == o.num_col_formats &&
               [this, &o] {
                   for(auto i = 0; i < num_col_formats; ++i) {
                       if(col_formats[i] != o.col_formats[i]) { return false; }
                       return true;
                   }
               }() &&
               dep_format == o.dep_format && culling == o.culling && depth_test == o.depth_test && depth_op == o.depth_op;
    }
    uint32_t num_col_formats{ 1 };
    std::array<VkFormat, 4> col_formats{ { VK_FORMAT_R8G8B8A8_SRGB } };
    VkFormat dep_format{ VK_FORMAT_D16_UNORM };
    VkCullModeFlags culling{ VK_CULL_MODE_BACK_BIT };
    bool depth_test{ false };
    VkCompareOp depth_op{ VK_COMPARE_OP_LESS };
};

struct RaytracingSettings {
    bool operator==(const RaytracingSettings& o) const {
        return recursion_depth == o.recursion_depth && sbt == o.sbt && groups.size() == o.groups.size() && [this, &o] {
            for(const auto& e : groups) {
                if(std::find_if(o.groups.begin(), o.groups.end(), [&e](auto& g) {
                       return e.type == g.type && e.generalShader == g.generalShader && e.closestHitShader == g.closestHitShader &&
                              e.anyHitShader == g.anyHitShader && e.intersectionShader == g.intersectionShader;
                   }) == o.groups.end()) {
                    return false;
                }
            }
            return true;
        }();
    }
    uint32_t recursion_depth{ 1 };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    Handle<Buffer> sbt;
};

struct Access {
    struct Resource {
        bool operator==(const Resource& o) const {
            return resource_idx == o.resource_idx && type == o.type && flags == o.flags;
        }
        bool operator<(const Resource& o) const {
            return (type < o.type) || (type == o.type && resource_idx < o.resource_idx);
        }
        uint32_t resource_idx{ ~0ul };
        Flags<ResourceType> type{};
        Flags<ResourceFlags> flags{};
    };

    bool operator==(const Access& o) const {
        return resource == o.resource && type == o.type && stage == o.stage && access == o.access &&
               layout == o.layout && count == o.count;
    }

    Resource resource{};
    Flags<AccessType> type{ AccessType::NONE_BIT };
    VkPipelineStageFlags2 stage{};
    VkAccessFlags2 access{};
    VkImageLayout layout{ VK_IMAGE_LAYOUT_MAX_ENUM };
    uint32_t count{ 1 };
};

struct RenderPass {
    std::vector<Access> accesses;
    std::vector<std::filesystem::path> shaders; // if empty - no pipeline generated (only for synchronization)
    std::variant<RasterizationSettings, RaytracingSettings> pipeline_settings;
    VkPipelineBindPoint pipeline_bind_point{}; // filled out during baking
    const Pipeline* pipeline{};                // filled out during baking
    Callback<void(VkCommandBuffer, uint32_t, RenderPass&)> callback_render;
};

class RenderGraph {
    struct Stage {
        std::vector<uint32_t> passes;
        std::vector<VkImageMemoryBarrier2> image_barriers;
        std::vector<VkBufferMemoryBarrier2> buffer_barriers;
        Callback<VkImage(uint32_t)> swapchain_barrier;
    };

  public:
    RenderGraph& add_pass(RenderPass pass);
    void bake();
    void create_pipeline(RenderPass& pass);
    void render(VkCommandBuffer cmd, uint32_t swapchain_index);

    std::vector<RenderPass> passes;
    std::vector<Stage> stages;
    std::deque<Pipeline> pipelines;
};
} // namespace rendergraph

struct Buffer {
    std::string name;
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VkDeviceAddress bda{};
    void* memory{};
    size_t capacity{ 0 };
    size_t size{ 0 };
    uint32_t alignment{ 0 };
    VkBufferUsageFlags usage{};
    bool is_bindless{ false };
};

struct FrameData {
    Semaphore sem_swapchain{};
    Semaphore sem_rendering_finished{};
    Fence fen_rendering_finished{};
    CommandPool* cmdpool{};
    rendergraph::RenderGraph render_graph;
    Handle<Buffer> constants{};
    GBuffer gbuffer{};
};

struct StagingBuffer {
    StagingBuffer();
    bool send(Buffer& dst, size_t dst_offset, std::span<const std::byte> src);
    bool send(Buffer& dst, size_t dst_offset, Buffer& src, size_t src_offset, size_t size);
    bool send(Image& dst, std::span<const std::byte> src, VkBufferImageCopy copy);
    void begin();
    void stage();
    CommandPool** pool{};
    VkCommandBuffer cmd{};
    Buffer buffer{};
};

class RendererVulkan : public Renderer {
  public:
    void init() final;

    void initialize_vulkan();
    void initialize_imgui();
    void initialize_resources();
    void create_window_sized_resources();

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
    void update_bindless_set();

    void build_blas();
    void build_tlas();
    void update_ddgi();

    // if sampler is null - it's storage image, if it's not - it's combined sampled
    Handle<Image> make_image(Image&& img, VkImageLayout layout, VkSampler sampler = nullptr);
    Handle<Image> make_image(Handle<Image> handle, VkImageLayout layout, VkSampler sampler = nullptr);
    Image& get_image(Handle<Image> handle);
    void destroy_image(const Image** img);
    Handle<Buffer> make_buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map = false,
                               uint32_t alignment = 1);
    Buffer allocate_buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map = false, uint32_t alignment = 1);
    void deallocate_buffer(Buffer& buffer);
    void destroy_buffer(Handle<Buffer> handle);
    void resize_buffer(Handle<Buffer> handle, size_t new_size);
    Buffer& get_buffer(Handle<Buffer> handle);
    void update_buffer(Handle<Buffer> handle);
    void send_to(Handle<Buffer> dst, size_t dst_offset, Handle<Buffer> src, size_t src_offset, size_t size);
    void send_to(Handle<Buffer> dst, size_t dst_offset, void* src, size_t size);
    void send_to(Handle<Buffer> dst, size_t dst_offset, std::span<const std::byte> bytes) {
        send_to(dst, dst_offset, (void*)bytes.data(), bytes.size_bytes());
    }
    template <typename... Ts> void send_many(Handle<Buffer> dst, size_t dst_offset, const Ts&... ts);

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
    Flags<RenderFlags> flags;
    SamplerStorage samplers;
    ScreenRect screen_rect;

    Queue gq;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;
    StagingBuffer* staging_buffer{};

    PipelineLayout bindless_layout{};
    VkDescriptorPool bindless_pool{};
    std::vector<Handle<Buffer>> bindless_buffers_to_update;

    HandleVector<RenderGeometry> geometries;
    HandleVector<GeometryMetadata> geometry_metadatas;
    HandleVector<RenderMesh> meshes;
    HandleVector<MeshMetadata> mesh_metadatas;

    HandleVector<RenderMaterial> materials;
    std::vector<components::Entity> mesh_instances;
    std::unordered_map<components::Entity, uint32_t> mesh_instance_idxs;
    std::vector<components::Entity> blas_instances;
    uint32_t max_draw_count{};

    VkAccelerationStructureKHR tlas{};
    Buffer tlas_buffer;
    Buffer tlas_instance_buffer;
    Buffer tlas_scratch_buffer;
    Handle<Buffer> vertex_positions_buffer;
    Handle<Buffer> vertex_attributes_buffer;
    Handle<Buffer> index_buffer;
    Handle<Buffer> indirect_draw_buffer;
    std::array<Handle<Buffer>, 2> transform_buffers;
    Buffer mesh_instance_mesh_id_buffer;
    Buffer tlas_mesh_offsets_buffer;
    Buffer tlas_transform_buffer;
    Buffer blas_mesh_offsets_buffer;
    Buffer triangle_geo_inst_id_buffer;
    Handle<Buffer> mesh_instances_buffer;

    Swapchain swapchain;
    std::array<FrameData, 2> frame_datas{};
    std::array<VkDescriptorSet, 2> bindless_sets;
    ShaderStorage shader_storage;
    std::deque<CommandPool> cmdpools;
    std::vector<Image> images;
    std::vector<Buffer> buffers;

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
