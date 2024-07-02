#pragma once

#include <vulkan/vulkan.hpp>
#include "renderer.hpp"
#include "vulkan_structs.hpp"

struct Buffer {
    constexpr Buffer() = default;

    Buffer(const std::string& name, uint64_t size, VkBufferUsageFlags usage, bool map);

    VkBuffer buffer{};
    VmaAllocation alloc{};
    void* mapped{};
    VkDeviceAddress bda{};
};

struct Image {
    Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers, VkFormat format,
          VkSampleCountFlagBits samples, VkImageUsageFlags usage);

    void transition_layout(VkImageLayout dst, bool from_undefined);

    VkImage image{};
    VmaAllocation alloc{};
    VkImageView view{};
    VkFormat format{};
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    uint32_t mips, layers;
};

class RendererVulkan : public Renderer {
  public:
    void render_model(Model& model) override;

    VkInstance instance;
    VkDevice dev;
    VkPhysicalDevice pdev;
    VmaAllocator vma;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;

    uint32_t gqi, pqi;
    VkQueue gq, pq;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchain_images;
    VkFormat swapchain_format;

    VkCommandPool cmdpool;
    VkCommandBuffer cmd;

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

    struct RenderingPrimitives {
        VkSemaphore sem_swapchain_image;
        VkSemaphore sem_tracing_done;
    } primitives;
};