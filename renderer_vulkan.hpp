#pragma once

#include <span>
#include <vulkan/vulkan.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <glm/mat4x3.hpp>
#include "renderer.hpp"
#include "vulkan_structs.hpp"
#include "handle_vector.hpp"

enum class VkRendererFlags : uint32_t { DIRTY_INSTANCES = 0x1, DIRTY_UPLOADS = 0x2, DIRTY_BLAS = 0x4, DIRTY_TLAS = 0x8 };
inline Flags<VkRendererFlags> operator|(VkRendererFlags a, VkRendererFlags b) { return Flags{ a } | b; }
inline Flags<VkRendererFlags> operator&(VkRendererFlags a, VkRendererFlags b) { return Flags{ a } & b; }

class Buffer {
  public:
    constexpr Buffer() = default;
    Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, size_t size, size_t alignment, VkBufferUsageFlags usage, bool map);

    size_t push_data(std::span<const std::byte> data);
    constexpr size_t get_free_space() const { return capacity - size; }

    size_t size{ 0 }, capacity{ 0 };
    VkBuffer buffer{};
    VmaAllocation alloc{};
    void* mapped{};
    VkDeviceAddress bda{};
};

struct Image {
    constexpr Image() = default;
    Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers, VkFormat format,
          VkSampleCountFlagBits samples, VkImageUsageFlags usage);

    void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                           VkAccessFlags2 dst_access, bool from_undefined, VkImageLayout dst_layout);

    VkImage image{};
    VmaAllocation alloc{};
    VkImageView view{};
    VkFormat format{};
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    uint32_t mips{}, layers{};
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 nor;
    glm::vec2 uv;
};

struct RenderMaterial {
    std::optional<uint32_t> color_texture;
    std::optional<uint32_t> normal_texture;
};

struct RenderMesh {
    uint32_t vertex_offset{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t index_offset{ 0 };
    uint32_t index_count{ 0 };
    uint32_t material{ 0 };
};

struct RenderModel {
    Flags<VkRendererFlags> flags;
    uint32_t first_mesh{ 0 };
    uint32_t mesh_count{ 0 };
    uint32_t first_vertex{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t first_index{ 0 };
    uint32_t index_count{ 0 };
    uint32_t first_material{ 0 };
    uint32_t material_count{ 0 };
    uint32_t first_texture{ 0 };
    uint32_t texture_count{ 0 };
};

struct RenderModelRTMetadata {
    VkAccelerationStructureKHR blas;
    Buffer blas_buffer;
};

struct RenderModelInstance {
    Handle<RenderModel> model;
    Flags<InstanceFlags> flags;
    glm::mat4x3 transform{ 1.0f };
    uint32_t batch_index{ 0 };
};

struct RenderInstanceBatch {
    Handle<RenderMesh> mesh;
    uint32_t first_instance{ 0 };
    uint32_t count{ 0 };
};

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

class RendererVulkan : public Renderer {
    struct BoundingBox {
        glm::vec3 center() const { return (max + min) * 0.5f; }
        glm::vec3 size() const { return (max - min); }
        glm::vec3 extent() const { return glm::abs(size() * 0.5f); }

        glm::vec3 min{ FLT_MAX }, max{ -FLT_MAX };
    };

    struct DDGI_Settings {
        BoundingBox probe_dims;
        float probe_distance{ 0.5f };
        glm::uvec3 probe_counts;
        glm::vec3 probe_walk;
        uint32_t irradiance_resolution{ 8 };
        Image irradiance_texture;
    };
    struct DDGI_Buffer {
        alignas(16) glm::vec3 probe_start;
        alignas(16) glm::uvec3 probe_counts;
        alignas(16) glm::vec3 probe_walk;
        alignas(16) float min_dist;
        float max_dist;
        float normal_bias;
        uint32_t irradiance_resolution;
        uint32_t rays_per_probe;
        uint32_t irr_tex_idx;
    };

  public:
    void init() final;

    void initialize_vulkan();
    void create_swapchain();

    void render() final;

    HandleBatchedModel batch_model(ImportedModel& model, BatchSettings settings) final;
    HandleInstancedModel instance_model(HandleBatchedModel model, InstanceSettings settings) final;
    void upload_model_textures();
    void upload_staged_models();
    void upload_instances();

    void compile_shaders();
    void build_rtpp();
    void build_sbt();
    void build_desc_sets();
    void create_rt_output_image();
    void build_blas();
    void build_tlas();
    void prepare_ddgi();

    VkCommandBuffer begin_recording(VkCommandPool pool, VkCommandBufferUsageFlags usage);
    void submit_recording(VkQueue queue, VkCommandBuffer buffer, const std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>>& wait_sems = {},
                          const std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>>& signal_sems = {}, VkFence fence = nullptr);
    void submit_recordings(VkQueue queue, const std::vector<RecordingSubmitInfo>& submits, VkFence fence = nullptr);
    void end_recording(VkCommandBuffer buffer);
    void reset_command_pool(VkCommandPool pool);
    VkCommandBuffer get_or_allocate_free_command_buffer(VkCommandPool pool);

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;

    vks::PhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    vks::PhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    uint32_t gqi, pqi;
    VkQueue gq, pq;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchain_images;
    VkFormat swapchain_format;

    VkCommandPool cmdpool;
    std::unordered_map<VkCommandPool, std::vector<VkCommandBuffer>> free_pool_buffers;
    std::unordered_map<VkCommandPool, std::vector<VkCommandBuffer>> used_pool_buffers;

    BoundingBox scene_bounding_box;
    VkAccelerationStructureKHR tlas;
    Buffer tlas_buffer;
    Buffer vertex_buffer, index_buffer;
    Buffer instance_data_buffer;

    std::vector<VkShaderModule> shader_modules;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    VkPipeline raytracing_pipeline;
    VkPipelineLayout raytracing_layout;
    VkDescriptorSetLayout raytracing_set_layout;
    VkDescriptorSet raytracing_set;
    VkDescriptorPool raytracing_pool;
    Buffer sbt;
    Buffer per_triangle_mesh_id_buffer;

    DDGI_Settings ddgi;
    Image rt_image;
    Buffer ubo;
    Buffer ddgi_buffer;

    std::vector<Image> textures;
    std::vector<RenderMaterial> materials;
    std::vector<RenderMesh> meshes;
    std::vector<RenderModelRTMetadata> rt_metadata;
    HandleVector<RenderModel> models;

    HandleVector<RenderModelInstance> model_instances;
    std::vector<RenderInstanceBatch> batches;

    Flags<VkRendererFlags> flags;
    struct UploadImage {
        std::string name;
        uint32_t width, height;
        std::vector<std::byte> rgba_data;
    };
    struct InstanceUpload {
        Handle<RenderMesh> batch;
        glm::mat4x3 transform{ 1.0f };
    };

    std::vector<Vertex> upload_vertices;
    std::vector<uint32_t> upload_indices;
    std::vector<UploadImage> upload_images;
    // std::vector<InstanceUpload> upload_positions;

    struct RenderingPrimitives {
        VkSemaphore sem_swapchain_image;
        VkSemaphore sem_tracing_done;
        VkSemaphore sem_copy_to_sw_img_done;
    } primitives;
};