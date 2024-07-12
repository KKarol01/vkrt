#pragma once

#include <span>
#include <vulkan/vulkan.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include "renderer.hpp"
#include "vulkan_structs.hpp"
#include "handle_vector.hpp"

enum class VkRendererFlags : uint32_t { DIRTY_INSTANCES = 0x1, DIRTY_UPLOADS = 0x2, DIRTY_BLAS = 0x4 };
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
    uint32_t index_offset{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t index_count{ 0 };
    uint32_t material{ 0 };
};

struct RenderModel {
    Flags<VkRendererFlags> flags;
    size_t first_mesh{ 0 };
    size_t mesh_count{ 0 };
    size_t first_vertex{ 0 };
    size_t vertex_count{ 0 };
    size_t first_index{ 0 };
    size_t index_count{ 0 };
    size_t first_material{ 0 };
    size_t material_count{ 0 };
    size_t first_texture{ 0 };
    size_t texture_count{ 0 };
};

struct RenderModelRTMetadata {
    VkAccelerationStructureKHR blas;
    Buffer blas_buffer;
};

struct ModelInstance {
    Handle<RenderModel> render_model;
};

struct RenderInstanceBatch {
    uint32_t instance_offset{ 0 };
    uint32_t count{ 0 };
};

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

class RendererVulkan : public Renderer {
  public:
    void init() override;

    void initialize_vulkan();
    void create_swapchain();

    void render() override;

    HandleBatchedModel batch_model(ImportedModel& model, BatchSettings settings) final;
    HandleInstancedModel instance_model(HandleBatchedModel model) final;
    void upload_model_textures();
    void upload_staged_models();

    void compile_shaders();
    void build_rtpp();
    void build_sbt();
    void build_desc_sets();
    void create_rt_output_image();
    void build_dirty_blases();
    void build_tlas();
    void build_descriptor_pool();
    void allocate_rtpp_descriptor_set();

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

    Image rt_image;
    Buffer ubo;

    std::vector<Image> textures;
    std::vector<RenderMaterial> materials;
    std::vector<RenderMesh> meshes;
    HandleVector<RenderModel> models;
    std::vector<RenderInstanceBatch> instances;
    std::vector<RenderModelRTMetadata> rt_metadata;

    Flags<VkRendererFlags> flags;
    struct UploadImage {
        std::string name;
        uint32_t width, height;
        std::vector<std::byte> rgba_data;
    };
    std::vector<Vertex> upload_vertices;
    std::vector<uint32_t> upload_indices;
    std::vector<UploadImage> upload_images;

    struct RenderingPrimitives {
        VkSemaphore sem_swapchain_image;
        VkSemaphore sem_tracing_done;
        VkSemaphore sem_copy_to_sw_img_done;
    } primitives;
};