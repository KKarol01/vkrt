#pragma once
#pragma once

#include <span>
#include <bitset>
#include <unordered_set>
#include <latch>
#include <vulkan/vulkan.hpp>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <glm/mat4x3.hpp>
#include "renderer.hpp"
#include "vulkan_structs.hpp"
#include "handle_vector.hpp"
#include "gpu_staging_manager.hpp"
#include "engine.hpp"

#define VK_CHECK(func)                                                                                                 \
    if(const auto res = func; res != VK_SUCCESS) { ENG_WARN("{}", #func); }

template <class... Ts> struct Visitor : Ts... {
    using Ts::operator()...;
};

enum class RendererFlags : uint32_t {
    DIRTY_MODEL_INSTANCES = 0x1,
    DIRTY_MODEL_BATCHES = 0x2,
    DIRTY_BLAS = 0x4,
    DIRTY_TLAS = 0x8,
    REFIT_TLAS = 0x10,
    UPDATE_MESH_POSITIONS = 0x20,
};

enum class RenderModelFlags : uint32_t { DIRTY_BLAS = 0x1 };

class Buffer {
  public:
    constexpr Buffer() = default;
    Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, size_t size, uint32_t alignment, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, const vks::BufferCreateInfo& create_info, const VmaAllocationCreateInfo& alloc_info);

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    bool push_data(std::span<const std::byte> data, uint32_t offset);
    bool push_data(std::span<const std::byte> data) { return push_data(data, size); }
    bool push_data(const void* data, size_t size_bytes) { return push_data(data, size_bytes, size); }
    bool push_data(const void* data, size_t size_bytes, size_t offset) {
        return push_data(std::span{ static_cast<const std::byte*>(data), size_bytes }, offset);
    }
    template <typename T> bool push_data(const std::vector<T>& vec) { return push_data(vec, size); }
    template <typename T> bool push_data(const std::vector<T>& vec, uint32_t offset) {
        return push_data(std::as_bytes(std::span{ vec }), offset);
    }
    void clear() { size = 0; }
    bool resize(size_t new_size);
    constexpr size_t get_free_space() const { return capacity - size; }

    std::string name;
    VkBufferUsageFlags usage{};
    size_t size{ 0 }, capacity{ 0 };
    uint32_t alignment{ 1 };
    VkBuffer buffer{};
    VmaAllocation alloc{};
    void* mapped{};
    VkDeviceAddress bda{};
};

struct Image {
    constexpr Image() = default;
    Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers,
          VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage);
    Image(const std::string& name, VkImage image, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips,
          uint32_t layers, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage);

    void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, bool from_undefined, VkImageLayout dst_layout);

    void _deduce_aspect(VkImageUsageFlags usage);
    void _create_default_view(int dims, VkImageUsageFlags usage);

    VkImage image{};
    VmaAllocation alloc{};
    VkImageView view{};
    VkFormat format{};
    VkImageAspectFlags aspect{};
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    uint32_t width{ 0 }, height{ 0 }, depth{ 0 };
    uint32_t mips{ 0 }, layers{ 0 };
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 nor;
    glm::vec2 uv;
};

struct RenderMaterial {
    uint32_t color_texture;
    uint32_t normal_texture;
};

// offsets and material are offsets relative to the model, not whole array
// thus, to calculate correct global vector index, do: mesh->model.first_vertex + mesh.vertex_offset
struct RenderMesh {
    uint32_t vertex_offset{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t index_offset{ 0 };
    uint32_t index_count{ 0 };
    uint32_t material{ 0 };
};

struct RenderModelRTMetadata {
    VkAccelerationStructureKHR blas{};
    Buffer blas_buffer{};
};

struct RenderModel {
    Flags<RenderModelFlags> flags{};
    Handle<RenderModelRTMetadata> rt_metadata;
    uint32_t first_vertex{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t first_index{ 0 };
    uint32_t index_count{ 0 };
    uint32_t first_mesh{ 0 };
    uint32_t mesh_count{ 0 };
    uint32_t first_material{ 0 };
    uint32_t material_count{ 0 };
    uint32_t first_texture{ 0 };
    uint32_t texture_count{ 0 };
};

struct RenderModelInstance {
    Handle<RenderModel> model{};
    Flags<InstanceFlags> flags{};
    glm::mat4x3 transform{ 1.0f };
    uint32_t tlas_instance_mask : 8 { 0xFF };
};

struct RenderMeshInstance {
    Handle<RenderModelInstance> model_instance;
    Handle<RenderMesh> mesh;
};

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

struct GPURenderMeshData {
    uint32_t vertex_offset{};
    uint32_t index_offset{};
    uint32_t color_texture_idx{};
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

enum class RendererPipelineType {
    DEFAULT_UNLIT,
    DDGI_PROBE_RAYCAST,
    DDGI_PROBE_UPDATE,
    DDGI_PROBE_OFFSET,
};

struct ShaderModuleWrapper {
    VkShaderModule module{};
    VkShaderStageFlagBits stage{};
};

struct RendererPipelineWrapper {
    VkPipeline pipeline{};
    VkPipelineLayout layout{};
    uint32_t rt_shader_group_count{ 0 };
};

struct RendererPipelineLayout {
    VkPipelineLayout layout{};
    std::vector<VkDescriptorSetLayout> descriptor_layouts;
    std::vector<VkDescriptorSetLayoutCreateFlags> layout_flags;
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindings;
    std::vector<std::vector<VkDescriptorBindingFlags>> binding_flags;
};

class RendererPipelineLayoutBuilder {
  public:
    RendererPipelineLayoutBuilder& add_set_binding(uint32_t set, uint32_t binding, uint32_t count,
                                                   VkDescriptorType type, VkDescriptorBindingFlags binding_flags = {},
                                                   VkShaderStageFlags stages = VK_SHADER_STAGE_ALL) {

        ENG_ASSERT(set < descriptor_layouts.size(), "Trying to access out of bounds descriptor set layout with idx: {}", set);

        if(set > 0 && descriptor_layouts.at(set - 1).bindings.empty()) {
            ENG_WARN("Settings descriptor set layout with idx {} while the previous descriptor set layout ({}) has "
                     "empty bindings.",
                     set, set - 1);
        }

        descriptor_layouts.at(set).bindings.emplace_back(binding, type, count, stages, nullptr);
        descriptor_layouts.at(set).binding_flags.emplace_back(binding_flags);
        return *this;
    }

    RendererPipelineLayoutBuilder& add_variable_descriptor_count(uint32_t set) {
        if(descriptor_layouts.size() <= set) {
            ENG_WARN("Trying to access out of bounds descriptor set layout with idx: {}", set);
            return *this;
        }

        descriptor_layouts.at(set).last_binding_of_variable_count = true;
        return *this;
    }

    RendererPipelineLayoutBuilder& set_push_constants(uint32_t size, VkShaderStageFlags stages = VK_SHADER_STAGE_ALL) {
        push_constants_size = size;
        push_constants_stage = stages;
        return *this;
    }

    RendererPipelineLayoutBuilder& set_layout_flags(uint32_t set, VkDescriptorSetLayoutCreateFlags layout_flags) {
        ENG_ASSERT(set < descriptor_layouts.size(), "Trying to access out of bounds descriptor set layout with idx: {}", set);
        descriptor_layout_flags.at(set) = layout_flags;
    }

    RendererPipelineLayout build();

  private:
    uint32_t push_constants_size{ 0 };
    VkShaderStageFlags push_constants_stage{};

    struct DescriptorLayout {
        VkDescriptorSetLayout layout{};
        bool last_binding_of_variable_count{ 0 };
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<VkDescriptorBindingFlags> binding_flags;
    };
    std::array<DescriptorLayout, 4> descriptor_layouts{};
    std::array<VkDescriptorSetLayoutCreateFlags, 4> descriptor_layout_flags{};
};

class RendererComputePipelineBuilder {
  public:
    RendererComputePipelineBuilder& set_stage(VkShaderModule module) {
        this->module = module;
        return *this;
    }

    RendererComputePipelineBuilder& set_layout(VkPipelineLayout layout) {
        this->layout = layout;
        return *this;
    }

    VkPipeline build();

  private:
    VkShaderModule module{};
    VkPipelineLayout layout{};
};

class RendererRaytracingPipelineBuilder {
  public:
    RendererRaytracingPipelineBuilder& add_raygen_stage(VkShaderModule module) {
        vks::RayTracingShaderGroupCreateInfoKHR group;
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = static_cast<uint32_t>(stages.size());
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
        add_stage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, module, group);
        return *this;
    }

    RendererRaytracingPipelineBuilder& add_closest_hit_stage(VkShaderModule module) {
        vks::RayTracingShaderGroupCreateInfoKHR group;
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = static_cast<uint32_t>(stages.size());
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
        add_stage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, module, group);
        return *this;
    }

    RendererRaytracingPipelineBuilder& add_miss_stage(VkShaderModule module) {
        vks::RayTracingShaderGroupCreateInfoKHR group;
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = static_cast<uint32_t>(stages.size());
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
        add_stage(VK_SHADER_STAGE_MISS_BIT_KHR, module, group);
        return *this;
    }

    RendererRaytracingPipelineBuilder& set_layout(VkPipelineLayout layout) {
        this->layout = layout;
        return *this;
    }

    RendererRaytracingPipelineBuilder& set_recursion_depth(uint32_t depth) {
        recursion_depth = depth;
        return *this;
    }

    VkPipeline build();

  private:
    void add_stage(VkShaderStageFlagBits stage, VkShaderModule module, vks::RayTracingShaderGroupCreateInfoKHR group) {
        vks::PipelineShaderStageCreateInfo info{};
        info.stage = stage;
        info.module = module;
        info.pName = "main";
        stages.push_back(info);
        shader_groups.push_back(group);
    }

    std::vector<vks::PipelineShaderStageCreateInfo> stages;
    std::vector<vks::RayTracingShaderGroupCreateInfoKHR> shader_groups;
    uint32_t recursion_depth{ 1 };
    VkPipelineLayout layout{};
};

class RendererGraphicsPipelineBuilder {
  public:
    RendererGraphicsPipelineBuilder& set_vertex_input(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset) {
        vertex_inputs.emplace_back(location, binding, format, offset);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_vertex_binding(uint32_t binding, uint32_t stride, VkVertexInputRate input_rate) {
        vertex_bindings.emplace_back(binding, stride, input_rate);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_depth_test(bool depth_write, VkCompareOp compare) {
        depth_test = true;
        this->depth_write = depth_write;
        depth_op = compare;
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_stencil_test(VkStencilOpState front, VkStencilOpState back) {
        stencil_test = true;
        stencil_front = front;
        stencil_back = back;
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_attachment_color_blending(VkBlendFactor src_col, VkBlendFactor dst_col,
                                                                   VkBlendOp col_op, VkBlendFactor src_a, VkBlendFactor dst_a,
                                                                   VkBlendOp a_op, VkColorComponentFlagBits col_write_mask) {
        color_blending_attachments.emplace_back(true, src_col, dst_col, col_op, src_a, dst_a, a_op, col_write_mask);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_dynamic_state(VkDynamicState state) {
        dynamic_states.push_back(state);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_stage(VkShaderStageFlagBits stage, VkShaderModule module) {
        shader_stages.emplace_back(stage, module);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_layout(VkPipelineLayout layout) {
        this->layout = layout;
        return *this;
    }

    RendererGraphicsPipelineBuilder& add_color_attachment_format(VkFormat format) {
        color_attachment_formats.push_back(format);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_depth_attachment_format(VkFormat format) {
        depth_attachment_format = format;
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_stencil_attachment_format(VkFormat format) {
        stencil_attachment_format = format;
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_viewport_count(uint32_t count) {
        viewport_count = count;
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_scissor_count(uint32_t count) {
        scissor_count = count;
        return *this;
    }

    VkPipeline build();

  private:
    std::vector<VkVertexInputAttributeDescription> vertex_inputs;
    std::vector<VkVertexInputBindingDescription> vertex_bindings;

    bool depth_test{ false }, depth_write{ false };
    VkCompareOp depth_op{};

    bool stencil_test{ false };
    VkStencilOpState stencil_front{}, stencil_back{};

    bool color_blending{ false };
    std::vector<VkPipelineColorBlendAttachmentState> color_blending_attachments;

    std::vector<VkDynamicState> dynamic_states;

    std::vector<std::pair<VkShaderStageFlagBits, VkShaderModule>> shader_stages;

    VkPipelineLayout layout{};

    std::vector<VkFormat> color_attachment_formats;
    VkFormat depth_attachment_format{ VK_FORMAT_UNDEFINED };
    VkFormat stencil_attachment_format{ VK_FORMAT_UNDEFINED };

    uint32_t viewport_count{}, scissor_count{};
};

class DescriptorPoolAllocator {
  public:
    VkDescriptorPool allocate_pool(const RendererPipelineLayout& layout, uint32_t set, uint32_t max_sets,
                                   VkDescriptorPoolCreateFlags flags = {});
    VkDescriptorSet allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout, uint32_t variable_count = 0);
    void reset_pool(VkDescriptorPool pool);

  private:
    std::vector<VkDescriptorPool> pools;
};

class DescriptorSetWriter {
    struct WriteImage {
        VkImageView view{};
        VkImageLayout layout{};
        VkSampler sampler{};
    };
    struct WriteBuffer {
        VkBuffer buffer{};
        uint32_t offset{};
        uint32_t range{};
    };
    struct WriteData {
        uint32_t binding{};
        uint32_t array_element{};
        std::variant<WriteImage, WriteBuffer, VkAccelerationStructureKHR> payload;
    };

  public:
    DescriptorSetWriter& write(uint32_t binding, uint32_t array_element, const Image& image, VkImageLayout layout);
    DescriptorSetWriter& write(uint32_t binding, uint32_t array_element, const Image& image, VkSampler sampler, VkImageLayout layout);
    DescriptorSetWriter& write(uint32_t binding, uint32_t array_element, VkImageView image, VkSampler sampler, VkImageLayout layout);
    DescriptorSetWriter& write(uint32_t binding, uint32_t array_element, const Buffer& buffer, uint32_t offset, uint32_t range);
    DescriptorSetWriter& write(uint32_t binding, uint32_t array_element, const VkAccelerationStructureKHR ac);
    DescriptorSetWriter& write(uint32_t binding, uint32_t array_element, const Image* imgs, uint32_t count,
                               VkSampler sampler, VkImageLayout layout) {
        for(uint32_t i = 0; i < count; ++i) {
            write(binding, array_element + i, imgs[i], sampler, layout);
        }
    }
    bool update(VkDescriptorSet set, const RendererPipelineLayout& layout, uint32_t set_idx);

  private:
    std::vector<WriteData> writes;
};

class SamplerStorage {
  public:
    VkSampler get_sampler();
    VkSampler get_sampler(VkFilter filter, VkSamplerAddressMode address);
    VkSampler get_sampler(vks::SamplerCreateInfo info);

  private:
    std::vector<std::pair<vks::SamplerCreateInfo, VkSampler>> samplers;
};

class ImageStatefulBarrier {
  public:
    constexpr ImageStatefulBarrier(Image& img, VkImageAspectFlags aspect, uint32_t base_mip, uint32_t mips,
                                   uint32_t base_layer, uint32_t layers, VkImageLayout start_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   VkPipelineStageFlags2 start_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                   VkAccessFlags2 start_access = VK_ACCESS_2_NONE)
        : image{ &img }, current_range{ aspect, base_mip, mips, base_layer, layers }, current_layout{ start_layout },
          current_stage{ start_stage }, current_access{ start_access } {}

    constexpr ImageStatefulBarrier(Image& img, VkImageLayout start_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   VkPipelineStageFlags2 start_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                   VkAccessFlags2 start_access = VK_ACCESS_2_NONE)
        : image{ &img }, current_range{ img.aspect, 0, img.mips, 0, img.layers }, current_layout{ start_layout },
          current_stage{ start_stage }, current_access{ start_access } {}

    void insert_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        insert_barrier(cmd, current_layout, dst_stage, dst_access, current_range);
    }

    void insert_barrier(VkCommandBuffer cmd, VkImageLayout dst_layout, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        insert_barrier(cmd, dst_layout, dst_stage, dst_access, current_range);
    }

  private:
    void insert_barrier(VkCommandBuffer cmd, VkImageLayout new_layout, VkPipelineStageFlags2 new_stage,
                        VkAccessFlags2 new_access, VkImageSubresourceRange new_range) {
        vks::ImageMemoryBarrier2 barrier;
        barrier.image = image->image;
        barrier.oldLayout = current_layout;
        barrier.newLayout = new_layout;
        barrier.srcStageMask = current_stage;
        barrier.srcAccessMask = current_access;
        barrier.dstStageMask = new_stage;
        barrier.dstAccessMask = new_access;
        barrier.subresourceRange = current_range;

        vks::DependencyInfo dep;
        dep.pImageMemoryBarriers = &barrier;
        dep.imageMemoryBarrierCount = 1;
        vkCmdPipelineBarrier2(cmd, &dep);

        current_range = new_range;
        current_layout = new_layout;
        current_stage = new_stage;
        current_access = new_access;
        image->current_layout = new_layout;
    }

    Image* image;
    VkImageSubresourceRange current_range;
    VkImageLayout current_layout;
    VkPipelineStageFlags2 current_stage;
    VkAccessFlags2 current_access;
};

class QueueScheduler {
  public:
    QueueScheduler() = default;
    QueueScheduler(VkQueue queue);

    void enqueue(const RecordingSubmitInfo& info, VkFence fence = nullptr);
    void enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence = nullptr);

  private:
    VkQueue vkqueue;
    friend class ThreadedQueueScheduler;
};

class ThreadedQueueScheduler {
    struct QueueItem {
        RecordingSubmitInfo info;
        VkFence fence;
        std::shared_ptr<std::latch> on_submit_latch;
    };

  public:
    ThreadedQueueScheduler() = default;
    ThreadedQueueScheduler(VkQueue queue);
    ThreadedQueueScheduler(ThreadedQueueScheduler&& other) noexcept;
    ThreadedQueueScheduler& operator=(ThreadedQueueScheduler&& other) noexcept;
    ~ThreadedQueueScheduler() noexcept;

    std::shared_ptr<std::latch> enqueue(const RecordingSubmitInfo& info, VkFence fence = nullptr);
    void enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence = nullptr);

  private:
    void submit_thread_func();

    QueueScheduler scheduler;
    std::jthread submit_thread;
    std::stop_token submit_thread_stop_token;
    std::mutex submit_queue_mutex;
    std::condition_variable submit_thread_cvar;
    std::vector<QueueItem> submit_queue;
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
        float probe_distance{ 0.4f };
        glm::uvec3 probe_counts;
        glm::vec3 probe_walk;
        int32_t irradiance_probe_side{ 6 };
        int32_t visibility_probe_side{ 14 };
        uint32_t rays_per_probe{ 64 };
        Image radiance_texture;
        Image irradiance_texture;
        Image visibility_texture;
        Image probe_offsets_texture;
    };

    struct DDGI_Buffer {
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
    };

    struct IndirectDrawCommandBufferHeader {
        uint32_t draw_count{};
        uint32_t mesh_instance_count{};
    };

  public:
    static RendererVulkan* get() { return static_cast<RendererVulkan*>(Engine::renderer()); }

    void init() final;

    void initialize_vulkan();
    void create_swapchain();
    void initialize_imgui();

    void render() final;

    HandleBatchedModel batch_model(const ImportedModel& model, BatchSettings settings) final;
    HandleInstancedModel instance_model(HandleBatchedModel model, InstanceSettings settings) final;
    void upload_model_textures();
    void upload_staged_models();
    void upload_instances();
    void upload_transforms();

    void update_transform(HandleInstancedModel model, glm::mat4x3 transform);

    void compile_shaders();
    void build_pipelines();
    void build_sbt();
    void create_rt_output_image();
    void build_blas();
    void build_tlas();
    void refit_tlas();
    void prepare_ddgi();

    VkCommandBuffer begin_recording(VkCommandPool pool, VkCommandBufferUsageFlags usage);
    void end_recording(VkCommandBuffer buffer);
    void reset_command_pool(VkCommandPool pool);

    uint32_t get_total_vertices() const {
        return models.empty() ? 0u : models.back().first_vertex + models.back().vertex_count;
    }
    uint32_t get_total_indices() const {
        return models.empty() ? 0u : models.back().first_index + models.back().index_count;
    }
    uint32_t get_total_triangles() const { return get_total_indices() / 3u; }

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;
    VkSurfaceKHR window_surface;

    vks::PhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    vks::PhysicalDeviceAccelerationStructurePropertiesKHR rt_acc_props;

    QueueScheduler scheduler_gq;
    uint32_t gqi, pqi, tqi1;
    VkQueue gq, pq, tq1;

    VkSwapchainKHR swapchain;
    std::vector<Image> swapchain_images;
    VkFormat swapchain_format;
    Image depth_buffers[2]{};

    VkCommandPool cmdpool;
    struct CommandBuffer {
        VkCommandBuffer command_buffer{};
        bool used{ false };
    };
    std::vector<CommandBuffer> command_buffers;

    GpuStagingManager staging;

    SamplerStorage samplers;

    BoundingBox scene_bounding_box;
    VkAccelerationStructureKHR tlas;
    Buffer tlas_buffer;
    Buffer tlas_instance_buffer;
    Buffer tlas_scratch_buffer;
    Buffer vertex_buffer, index_buffer;
    Buffer indirect_draw_buffer;
    Buffer* mesh_instance_transform_buffer[2]; // memory leak
    Buffer mesh_instance_mesh_id_buffer;

    std::unordered_map<ShaderModuleType, ShaderModuleWrapper> shader_modules;
    std::unordered_map<RendererPipelineType, RendererPipelineWrapper> pipelines;
    std::vector<RendererPipelineLayout> layouts;
    DescriptorPoolAllocator descriptor_pool_allocator;
    VkDescriptorPool per_frame_desc_pool;
    VkDescriptorPool imgui_desc_pool;

    Buffer sbt;

    Buffer tlas_mesh_offsets_buffer;
    Buffer tlas_transform_buffer;
    Buffer blas_mesh_offsets_buffer;
    Buffer triangle_mesh_ids_buffer;
    Buffer mesh_datas_buffer;

    DDGI_Settings ddgi;
    Image rt_image;
    Buffer global_buffer;
    Buffer ddgi_buffer;
    Buffer ddgi_debug_probe_offsets_buffer;

    HandleVector<RenderModel> models;
    std::vector<RenderMesh> meshes;
    std::vector<RenderMaterial> materials;
    std::vector<Image> textures;
    std::vector<BoundingBox> model_bbs;
    HandleVector<RenderModelRTMetadata> rt_metadata;

    HandleVector<RenderModelInstance> model_instances;
    std::vector<RenderMeshInstance> mesh_instances;

    Flags<RendererFlags> flags;
    struct UploadImage {
        uint64_t image_index;
        std::vector<std::byte> rgba_data;
    };

    std::vector<Vertex> upload_vertices;
    std::vector<uint32_t> upload_indices;
    std::vector<UploadImage> upload_images;
    std::vector<std::pair<Handle<RenderModelInstance>, glm::mat4x3>> upload_positions;

    uint32_t num_frame{ 0 };

    struct RenderingPrimitives {
        VkSemaphore sem_swapchain_image;
        VkSemaphore sem_rendering_finished;
        VkSemaphore sem_gui_start;
        VkSemaphore sem_copy_to_sw_img_done;
    } primitives;
};