#pragma once

#include <span>
#include <vulkan/vulkan.hpp>
#include "renderer.hpp"
#include "vulkan_structs.hpp"

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
                           VkAccessFlags2 dst_access, bool from_undefined, VkImageLayout dst_layout

    );

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
    size_t first_mesh{ 0 };
    size_t mesh_count{ 0 };
    size_t vertex_count{ 0 };
    size_t index_count{ 0 };
};

struct ModelInstance {
    uint32_t render_model;
};

class RendererVulkan : public Renderer {
  public:
    void init() override;

    void initialize_vulkan();
    void create_swapchain();

    void render() override;
    void batch_model(ImportedModel& model, BatchSettings settings) override;
    void compile_shaders();
    void build_rtpp();
    void build_sbt();
    void build_desc_sets();
    void create_rt_output_image();
    void build_blas(RenderModel rm);
    void build_tlas();

    VkCommandBuffer begin_recording(VkCommandPool pool, VkCommandBufferUsageFlags usage);
    void submit_recording(VkQueue queue, VkCommandBuffer buffer);
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
    VkCommandBuffer cmd;
    std::unordered_map<VkCommandPool, std::vector<VkCommandBuffer>> free_pool_buffers;
    std::unordered_map<VkCommandPool, std::vector<VkCommandBuffer>> used_pool_buffers;

    VkAccelerationStructureKHR tlas, blas;
    Buffer blas_buffer;
    Buffer tlas_buffer;
    Buffer vertex_buffer, index_buffer;

    std::vector<VkShaderModule> shader_modules;

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    VkPipeline raytracing_pipeline;
    VkPipelineLayout raytracing_layout;
    VkDescriptorSetLayout raytracing_set_layout;
    VkDescriptorSet raytracing_set;
    Buffer sbt;

    Image rt_image;
    Buffer ubo;

    std::vector<Image> textures;
    std::vector<RenderMaterial> materials;
    std::vector<RenderModel> models;
    std::vector<RenderMesh> meshes;
    std::vector<ModelInstance> instances;

    struct RenderingPrimitives {
        VkSemaphore sem_swapchain_image;
        VkSemaphore sem_tracing_done;
    } primitives;
};