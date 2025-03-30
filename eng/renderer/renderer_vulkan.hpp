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
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/handle_vec.hpp>
#include <eng/renderer/renderer_vulkan_wrappers.hpp>
#include <eng/engine.hpp>
#include <eng/common/callback.hpp>
#include <eng/renderer/descpool.hpp>
#include <eng/renderer/buffer.hpp>
#include <eng/renderer/image.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/staging_buffer.hpp>

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
    // RESIZE_SCREEN_RECT_BIT = 0x80,
};

enum class GeometryFlags : uint32_t { DIRTY_BLAS_BIT = 0x1 };
enum class RenderMeshFlags : uint32_t {};

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
    std::vector<VkImageView> views;
};

struct GBuffer {
    Handle<Image> color_image{};
    Handle<Image> view_space_positions_image{};
    Handle<Image> view_space_normals_image{};
    Handle<Image> depth_buffer_image{};
    VkImageView view_depth_buffer_image_ronly_lr{};
    Handle<Image> ambient_occlusion_image{};
};

struct PipelineLayout {
    VkPipelineLayout layout{};
    VkDescriptorSetLayout descriptor_layout{};
};

struct Pipeline {
    VkPipeline pipeline{};
};

namespace rendergraph {

constexpr uint32_t swapchain_index = ~0ul;

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
    VkFormat dep_format{ VK_FORMAT_D24_UNORM_S8_UINT };
    VkCullModeFlags culling{ VK_CULL_MODE_BACK_BIT };
    bool depth_test{ false };
    bool depth_write{ true };
    VkCompareOp depth_op{ VK_COMPARE_OP_LESS };
};

struct RaytracingSettings {
    bool operator==(const RaytracingSettings& o) const {
        return recursion_depth == o.recursion_depth && sbt_buffer == o.sbt_buffer && groups.size() == o.groups.size() &&
               [this, &o] {
                   for(const auto& e : groups) {
                       if(std::find_if(o.groups.begin(), o.groups.end(), [&e](auto& g) {
                              return e.type == g.type && e.generalShader == g.generalShader &&
                                     e.closestHitShader == g.closestHitShader && e.anyHitShader == g.anyHitShader &&
                                     e.intersectionShader == g.intersectionShader;
                          }) == o.groups.end()) {
                           return false;
                       }
                   }
                   return true;
               }();
    }
    uint32_t recursion_depth{ 1 };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    Handle<Buffer> sbt_buffer;
};

struct Access {
    // bool operator==(const Access& o) const {
    //     return resource == o.resource && resource_flags == o.resource_flags && type == o.type && stage == o.stage &&
    //            access == o.access && layout == o.layout && count == o.count;
    // }

    std::variant<Handle<Buffer>, Handle<Image>> resource;
    Flags<ResourceFlags> flags{};
    Flags<AccessType> type{};
    VkPipelineStageFlags2 stage{};
    VkAccessFlags2 access{};
    VkImageLayout layout{ VK_IMAGE_LAYOUT_MAX_ENUM };
    // uint32_t count{ 1 };
};

struct RenderPass {
    std::string name;
    std::vector<Access> accesses;
    std::vector<std::filesystem::path> shaders; // if empty - no pipeline generated (only for synchronization)
    std::variant<RasterizationSettings, RaytracingSettings> pipeline_settings;
    Callback<void(VkCommandBuffer, uint32_t, RenderPass&)> callback_render;
    VkPipelineBindPoint pipeline_bind_point{}; // filled out during baking
    const Pipeline* pipeline{};                // filled out during baking
};

class RenderGraph {
    struct Stage {
        std::vector<RenderPass*> passes;
        std::vector<VkImageMemoryBarrier2> image_barriers;
        std::vector<VkBufferMemoryBarrier2> buffer_barriers;
        Callback<Image&(uint32_t)> get_swapchain_image_callback;
    };

  public:
    RenderPass* make_pass(const std::string& name);
    RenderGraph& add_pass(RenderPass* pass);
    void bake();
    void create_pipeline(RenderPass& pass);
    void render(VkCommandBuffer cmd, uint32_t swapchain_index);

    std::deque<RenderPass> passes;
    std::deque<Pipeline> pipelines;
    std::vector<RenderPass*> render_list;
    std::vector<Stage> stages;
};
} // namespace rendergraph

struct FrameData {
    CommandPool* cmdpool{};
    rendergraph::RenderGraph render_graph;
    VkSemaphore sem_swapchain{};
    VkSemaphore sem_rendering_finished{};
    VkFence fen_rendering_finished{};
    Handle<Buffer> constants{};
    Handle<Buffer> transform_buffers{};
    GBuffer gbuffer{};
};

// struct StagingBuffer {
//     StagingBuffer();
//     bool send(Buffer& dst, size_t dst_offset, std::span<const std::byte> src);
//     bool send(Buffer& dst, size_t dst_offset, Buffer& src, size_t src_offset, size_t size);
//     bool send(Image& dst, std::span<const std::byte> src, VkBufferImageCopy copy);
//     void begin();
//     void stage();
//     CommandPool** pool{};
//     VkCommandBuffer cmd{};
//     Buffer buffer{};
// };

//enum class BindlessType : uint32_t { NONE, STORAGE_BUFFER, STORAGE_IMAGE, COMBINED_IMAGE };
//
//struct BindlessEntry {
//    bool operator==(const BindlessEntry& a) const {
//        return resource_handle == a.resource_handle && type == a.type && layout == a.layout && sampler == a.sampler;
//    }
//    VkDescriptorType to_vk_descriptor_type() const {
//        return type == BindlessType::STORAGE_BUFFER   ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
//               : type == BindlessType::STORAGE_IMAGE  ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
//               : type == BindlessType::COMBINED_IMAGE ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
//                                                      : VK_DESCRIPTOR_TYPE_MAX_ENUM;
//    }
//    uint32_t resource_handle{};
//    BindlessType type{};
//    VkImageLayout layout{};
//    VkSampler sampler{};
//};

class RendererVulkan : public Renderer {
  public:
    static RendererVulkan* get_instance() { return static_cast<RendererVulkan*>(Engine::get().renderer); }

    ~RendererVulkan() override = default;

    void init() final;
    void initialize_vulkan();
    void initialize_imgui();
    void initialize_resources();
    void create_window_sized_resources();
    void build_render_graph();

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
    size_t get_imgui_texture_id(Handle<Image> handle, ImageFilter filter, ImageAddressing addressing) final;
    Handle<Image> get_color_output_texture() const final;
    RenderMaterial get_material(Handle<RenderMaterial> handle) const final;
    VsmData& get_vsm_data() final;

    void upload_model_textures();
    void upload_staged_models();
    void bake_indirect_commands();
    void upload_transforms();
    //void update_bindless_set();

    void build_blas();
    void build_tlas();
    void update_ddgi();

    // Image allocate_image(const std::string& name, VkFormat format, VkImageType type, VkExtent3D extent, uint32_t mips,
    //                      uint32_t layers, VkImageUsageFlags usage);
    // Buffer allocate_buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map = false, uint32_t alignment = 1);
    Handle<Buffer> make_buffer(const std::string& name, const VkBufferCreateInfo& vk_info, const VmaAllocationCreateInfo& vma_info);
    Handle<Image> make_image(const std::string& name, const VkImageCreateInfo& vk_info);
    VkImageView make_image_view(Handle<Image> handle, VkImageLayout layout, VkSampler sampler);
    VkImageView make_image_view(Handle<Image> handle, const VkImageViewCreateInfo& vk_info, VkImageLayout layout, VkSampler sampler);
    Buffer& get_buffer(Handle<Buffer> handle);
    Image& get_image(Handle<Image> handle);
    void destroy_buffer(Handle<Buffer> buffer);
    void replace_buffer(Handle<Buffer> dst_buffer, Buffer&& src_buffer);
    uint32_t register_bindless_index(Handle<Buffer> handle);
    uint32_t register_bindless_index(VkImageView view, VkImageLayout layout, VkSampler sampler);
    uint32_t get_bindless_index(Handle<Buffer> handle);
    //uint32_t get_bindless_index(Handle<Image> handle, VkImageLayout layout, VkSampler sampler);
    uint32_t get_bindless_index(VkImageView view);

    // void update_bindless_resource(Handle<Image> handle);
    // void update_bindless_resource(Handle<Buffer> handle);

    // void destroy_image(const Image** img);
    // void deallocate_buffer(Buffer& buffer);
    // void destroy_buffer(Handle<Buffer> handle);
    // void resize_buffer(Handle<Buffer> handle, size_t new_size);

    // void send_to(Handle<Buffer> dst, size_t dst_offset, Handle<Buffer> src, size_t src_offset, size_t size);
    // void send_to(Handle<Buffer> dst, size_t dst_offset, void* src, size_t size);
    // void send_to(Handle<Buffer> dst, size_t dst_offset, std::span<const std::byte> bytes) {
    //     send_to(dst, dst_offset, (void*)bytes.data(), bytes.size_bytes());
    // }
    // template <typename... Ts> void send_many(Handle<Buffer> dst, size_t dst_offset, const Ts&... ts);

    FrameData& get_frame_data(uint32_t offset = 0);
    const FrameData& get_frame_data(uint32_t offset = 0) const;

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

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    SubmitQueue submit_queue;
    StagingBuffer staging_buffer;
    BindlessDescriptorPool bindless_pool{};

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
    Handle<Buffer> tlas_buffer;
    Handle<Buffer> tlas_instance_buffer;
    Handle<Buffer> tlas_scratch_buffer;
    VsmData vsm;
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

    ShaderStorage shader_storage;
    SamplerStorage samplers;
    std::unordered_map<Handle<Image>, Image> images;
    std::unordered_map<Handle<Buffer>, Buffer> buffers;
    // todo: handle recycling

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