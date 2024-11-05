#pragma once
#include <vector>
#include <string>
#include <variant>
#include <utility>
#include <vulkan/vulkan.h>
#include "common/types.hpp"

class Buffer {
  public:
    constexpr Buffer() = default;
    Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, size_t size, u32 alignment, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, vks::BufferCreateInfo create_info, VmaAllocationCreateInfo alloc_info, u32 alignment);

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    bool push_data(std::span<const std::byte> data, u32 offset);
    bool push_data(std::span<const std::byte> data) { return push_data(data, size); }
    bool push_data(const void* data, size_t size_bytes) { return push_data(data, size_bytes, size); }
    bool push_data(const void* data, size_t size_bytes, size_t offset) {
        return push_data(std::span{ static_cast<const std::byte*>(data), size_bytes }, offset);
    }
    template <typename T> bool push_data(const std::vector<T>& vec) { return push_data(vec, size); }
    template <typename T> bool push_data(const std::vector<T>& vec, u32 offset) {
        return push_data(std::as_bytes(std::span{ vec }), offset);
    }

    void clear() { size = 0; }
    bool resize(size_t new_size);
    constexpr size_t get_free_space() const { return capacity - size; }
    void deallocate();

    std::string name;
    VkBufferUsageFlags usage{};
    u64 size{};
    u64 capacity{};
    u32 alignment{ 1 };
    VkBuffer buffer{};
    VmaAllocation alloc{};
    void* mapped{};
    VkDeviceAddress bda{};
};

class Image {
  public:
    constexpr Image() = default;
    Image(const std::string& name, u32 width, u32 height, u32 depth, u32 mips, u32 layers, VkFormat format,
          VkSampleCountFlagBits samples, VkImageUsageFlags usage = {});
    Image(const std::string& name, VkImage image, u32 width, u32 height, u32 depth, u32 mips, u32 layers,
          VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage);
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout dst_layout);
    void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout src_layout, VkImageLayout dst_layout);

    void _deduce_aspect(VkImageUsageFlags usage);
    void _create_default_view(int dims, VkImageUsageFlags usage);

    VkImage image{};
    VmaAllocation alloc{};
    VkImageView view{};
    VkFormat format{};
    VkImageAspectFlags aspect{};
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageUsageFlags usage{};
    u32 width{};
    u32 height{};
    u32 depth{};
    u32 mips{};
    u32 layers{};
};

struct RenderPipelineLayout {
    VkPipelineLayout layout{};
    std::vector<VkDescriptorSetLayout> descriptor_layouts;
    std::vector<VkDescriptorSetLayoutCreateFlags> layout_flags;
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindings;
    std::vector<std::vector<VkDescriptorBindingFlags>> binding_flags;
};

class RendererPipelineLayoutBuilder {
  public:
    RendererPipelineLayoutBuilder& add_set_binding(u32 set, u32 binding, u32 count, VkDescriptorType type,
                                                   VkDescriptorBindingFlags binding_flags = {},
                                                   VkShaderStageFlags stages = VK_SHADER_STAGE_ALL);
    RendererPipelineLayoutBuilder& add_variable_descriptor_count(u32 set);
    RendererPipelineLayoutBuilder& set_push_constants(u32 size, VkShaderStageFlags stages = VK_SHADER_STAGE_ALL);
    RendererPipelineLayoutBuilder& set_layout_flags(u32 set, VkDescriptorSetLayoutCreateFlags layout_flags);
    RenderPipelineLayout build();

  private:
    u32 push_constants_size{ 0 };
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
        group.generalShader = static_cast<u32>(stages.size());
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
        group.closestHitShader = static_cast<u32>(stages.size());
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
        add_stage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, module, group);
        return *this;
    }

    RendererRaytracingPipelineBuilder& add_miss_stage(VkShaderModule module) {
        vks::RayTracingShaderGroupCreateInfoKHR group;
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = static_cast<u32>(stages.size());
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

    RendererRaytracingPipelineBuilder& set_recursion_depth(u32 depth) {
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
    u32 recursion_depth{ 1 };
    VkPipelineLayout layout{};
};

class RendererGraphicsPipelineBuilder {
  public:
    RendererGraphicsPipelineBuilder& set_vertex_input(u32 location, u32 binding, VkFormat format, u32 offset) {
        vertex_inputs.emplace_back(location, binding, format, offset);
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_vertex_binding(u32 binding, u32 stride, VkVertexInputRate input_rate) {
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

    RendererGraphicsPipelineBuilder& set_viewport_count(u32 count) {
        viewport_count = count;
        return *this;
    }

    RendererGraphicsPipelineBuilder& set_scissor_count(u32 count) {
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

    u32 viewport_count{}, scissor_count{};
};

class DescriptorPoolAllocator {
    struct DescriptorSet {
        VkDescriptorSet set;
        VkDescriptorSetLayout layout;
        bool free{ true };
    };
    struct PoolDescriptor {
        std::vector<DescriptorSet> sets;
    };

  public:
    VkDescriptorPool allocate_pool(const RenderPipelineLayout& layout, u32 set, u32 max_sets,
                                   VkDescriptorPoolCreateFlags flags = {});
    VkDescriptorSet allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout, u32 variable_count = 0);
    void reset_pool(VkDescriptorPool pool);

    std::unordered_map<VkDescriptorPool, PoolDescriptor> pools;
};

class DescriptorSetWriter {
    struct WriteImage {
        VkImageView view{};
        VkImageLayout layout{};
        VkSampler sampler{};
    };
    struct WriteBuffer {
        VkBuffer buffer{};
        u32 offset{};
        u32 range{};
    };
    struct WriteData {
        u32 binding{};
        u32 array_element{};
        std::variant<WriteImage, WriteBuffer, VkAccelerationStructureKHR> payload;
    };

  public:
    DescriptorSetWriter& write(u32 binding, u32 array_element, const Image& image, VkImageLayout layout);
    DescriptorSetWriter& write(u32 binding, u32 array_element, const Image& image, VkSampler sampler, VkImageLayout layout);
    DescriptorSetWriter& write(u32 binding, u32 array_element, VkImageView image, VkSampler sampler, VkImageLayout layout);
    DescriptorSetWriter& write(u32 binding, u32 array_element, const Buffer& buffer, u32 offset, u32 range);
    DescriptorSetWriter& write(u32 binding, u32 array_element, const VkAccelerationStructureKHR ac);
    DescriptorSetWriter& write(u32 binding, u32 array_element, const Image* imgs, u32 count, VkSampler sampler, VkImageLayout layout) {
        for(u32 i = 0; i < count; ++i) {
            write(binding, array_element + i, imgs[i], sampler, layout);
        }
        return *this;
    }
    bool update(VkDescriptorSet set, const RenderPipelineLayout& layout, u32 set_idx);

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
    constexpr ImageStatefulBarrier(Image& img, VkImageAspectFlags aspect, u32 base_mip, u32 mips, u32 base_layer,
                                   u32 layers, VkImageLayout start_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   VkPipelineStageFlags2 start_stage = VK_PIPELINE_STAGE_2_NONE,
                                   VkAccessFlags2 start_access = VK_ACCESS_2_NONE)
        : image{ &img }, current_range{ aspect, base_mip, mips, base_layer, layers }, current_layout{ start_layout },
          current_stage{ start_stage }, current_access{ start_access } {}

    constexpr ImageStatefulBarrier(Image& img, VkImageLayout start_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   VkPipelineStageFlags2 start_stage = VK_PIPELINE_STAGE_2_NONE,
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

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

class QueueScheduler {
  public:
    QueueScheduler() = default;
    QueueScheduler(VkQueue queue);

    void enqueue(const RecordingSubmitInfo& info, VkFence fence = nullptr);
    void enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence = nullptr);

  private:
    VkQueue vkqueue;
};

class CommandPool {
  public:
    constexpr CommandPool() noexcept = default;
    CommandPool(u32 queue_index, VkCommandPoolCreateFlags flags = {});
    ~CommandPool() noexcept;

    CommandPool(const CommandPool&) noexcept = delete;
    CommandPool& operator=(const CommandPool&) noexcept = delete;
    CommandPool(CommandPool&& other) noexcept;
    CommandPool& operator=(CommandPool&& other) noexcept;

    VkCommandBuffer allocate(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin(VkCommandBufferUsageFlags flags = {}, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin_onetime(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    void end(VkCommandBuffer buffer);
    void reset();

    std::vector<std::pair<VkCommandBuffer, bool>> buffers;
    VkCommandPool cmdpool{};
};
