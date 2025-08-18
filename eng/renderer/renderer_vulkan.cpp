#include <filesystem>
#include <bitset>
#include <numeric>
#include <fstream>
#include <utility>
#include <chrono>
#include <meshoptimizer/src/meshoptimizer.h>
#include <stb/stb_include.h>
#include <volk/volk.h>
#include <glm/mat3x3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <vk-bootstrap/src/VkBootstrap.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <eng/engine.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/utils.hpp>
#include <assets/shaders/bindless_structures.inc.glsl>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/passes/passes.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/common/to_vk.hpp>
#include <eng/common/paths.hpp>
#include <eng/common/to_string.hpp>

// https://www.shadertoy.com/view/WlSSWc
static float halton(int i, int b)
{
    /* Creates a halton sequence of values between 0 and 1.
        https://en.wikipedia.org/wiki/Halton_sequence
        Used for jittering based on a constant set of 2D points. */
    float f = 1.0;
    float r = 0.0;
    while(i > 0)
    {
        f = f / float(b);
        r = r + f * float(i % b);
        i = i / b;
    }

    return r;
}

namespace eng
{

namespace gfx
{

void VkPipelineMetadata::init(Pipeline& a)
{
    if(a.metadata) { return; }
    auto* md = new VkPipelineMetadata{};
    a.metadata = md;
    {
        const auto stage = a.info.shaders[0]->stage;
        if(stage == ShaderStage::VERTEX_BIT) { md->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS; }
        else if(stage == ShaderStage::COMPUTE_BIT) { md->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE; }
        else
        {
            assert(false);
            delete md;
            a.metadata = nullptr;
            return;
        }
    }

    auto* r = RendererVulkan::get_instance();
    md->layout = r->bindless_pool->get_pipeline_layout();

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(a.info.shaders.size());
    for(const auto& e : a.info.shaders)
    {
        stages.push_back(Vks(VkPipelineShaderStageCreateInfo{
            .stage = gfx::to_vk(e->stage), .module = ((ShaderMetadata*)e->metadata)->shader, .pName = "main" }));
    }

    if(md->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        const auto vkinfo = Vks(VkComputePipelineCreateInfo{ .stage = stages.at(0), .layout = md->layout });
        VK_CHECK(vkCreateComputePipelines(r->dev, {}, 1, &vkinfo, {}, &md->pipeline));
        return;
    }

    std::vector<VkVertexInputBindingDescription> vkbindings(a.info.bindings.size());
    for(auto i = 0u; i < a.info.bindings.size(); ++i)
    {
        vkbindings.at(i) = { a.info.bindings.at(i).binding, a.info.bindings.at(i).stride,
                             a.info.bindings.at(i).instanced ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX };
    }
    std::vector<VkVertexInputAttributeDescription> vkattributes(a.info.attributes.size());
    for(auto i = 0u; i < a.info.attributes.size(); ++i)
    {
        vkattributes.at(i) = { a.info.attributes.at(i).location, a.info.attributes.at(i).binding,
                               gfx::to_vk(a.info.attributes.at(i).format), a.info.attributes.at(i).offset };
    }
    auto pVertexInputState =
        Vks(VkPipelineVertexInputStateCreateInfo{ .vertexBindingDescriptionCount = (uint32_t)a.info.bindings.size(),
                                                  .pVertexBindingDescriptions = vkbindings.data(),
                                                  .vertexAttributeDescriptionCount = (uint32_t)a.info.attributes.size(),
                                                  .pVertexAttributeDescriptions = vkattributes.data() });

    auto pInputAssemblyState = Vks(VkPipelineInputAssemblyStateCreateInfo{ .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST });

    auto pTessellationState = Vks(VkPipelineTessellationStateCreateInfo{});

    auto pViewportState = Vks(VkPipelineViewportStateCreateInfo{});

    auto pRasterizationState = Vks(VkPipelineRasterizationStateCreateInfo{
        .polygonMode = gfx::to_vk(a.info.polygon_mode),
        .cullMode = gfx::to_vk(a.info.culling),
        .frontFace = a.info.front_is_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = a.info.line_width,
    });

    auto pMultisampleState = Vks(VkPipelineMultisampleStateCreateInfo{
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    });

    // vkstencil
    auto pDepthStencilState = Vks(VkPipelineDepthStencilStateCreateInfo{
            .depthTestEnable = a.info.depth_test,
            .depthWriteEnable = a.info.depth_write,
            .depthCompareOp = gfx::to_vk(a.info.depth_compare),
            .depthBoundsTestEnable = false,
            .stencilTestEnable = a.info.stencil_test,
            .front = {
                gfx::to_vk(a.info.stencil_front.fail),
                gfx::to_vk(a.info.stencil_front.pass),
                gfx::to_vk(a.info.stencil_front.depth_fail),
                gfx::to_vk(a.info.stencil_front.compare),
                a.info.stencil_front.compare_mask,
                a.info.stencil_front.write_mask,
                a.info.stencil_front.ref,
            },
            .back = {
                gfx::to_vk(a.info.stencil_back.fail),
                gfx::to_vk(a.info.stencil_back.pass),
                gfx::to_vk(a.info.stencil_back.depth_fail),
                gfx::to_vk(a.info.stencil_back.compare),
                a.info.stencil_back.compare_mask,
                a.info.stencil_back.write_mask,
                a.info.stencil_back.ref,
            },
        });

    std::array<VkPipelineColorBlendAttachmentState, 8> vkblends;
    std::array<VkFormat, 8> vkcol_formats;
    for(uint32_t i = 0; i < a.info.attachments.count; ++i)
    {
        vkblends.at(i) = { a.info.attachments.blend_states.at(i).enable,
                           gfx::to_vk(a.info.attachments.blend_states.at(i).src_color_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).dst_color_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).color_op),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).src_alpha_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).dst_alpha_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).alpha_op),
                           VkColorComponentFlags{ ((uint32_t)a.info.attachments.blend_states.at(i).r) << 0 |
                                                  ((uint32_t)a.info.attachments.blend_states.at(i).g) << 1 |
                                                  ((uint32_t)a.info.attachments.blend_states.at(i).b) << 2 |
                                                  ((uint32_t)a.info.attachments.blend_states.at(i).a) << 3 } };
        vkcol_formats.at(i) = gfx::to_vk(a.info.attachments.color_formats.at(i));
    }
    auto pColorBlendState = Vks(VkPipelineColorBlendStateCreateInfo{
        .attachmentCount = a.info.attachments.count,
        .pAttachments = vkblends.data(),
    });

    VkDynamicState dynstates[]{
        VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
        VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
    };
    auto pDynamicState = Vks(VkPipelineDynamicStateCreateInfo{
        .dynamicStateCount = sizeof(dynstates) / sizeof(dynstates[0]),
        .pDynamicStates = dynstates,
    });

    auto pDynamicRendering = Vks(VkPipelineRenderingCreateInfo{
        .colorAttachmentCount = a.info.attachments.count,
        .pColorAttachmentFormats = vkcol_formats.data(),
        .depthAttachmentFormat = gfx::to_vk(a.info.attachments.depth_format),
        .stencilAttachmentFormat = gfx::to_vk(a.info.attachments.stencil_format),
    });

    auto vk_info = Vks(VkGraphicsPipelineCreateInfo{
        .pNext = &pDynamicRendering,
        .stageCount = (uint32_t)stages.size(),
        .pStages = stages.data(),
        .pVertexInputState = &pVertexInputState,
        .pInputAssemblyState = &pInputAssemblyState,
        .pTessellationState = &pTessellationState,
        .pViewportState = &pViewportState,
        .pRasterizationState = &pRasterizationState,
        .pMultisampleState = &pMultisampleState,
        .pDepthStencilState = &pDepthStencilState,
        .pColorBlendState = &pColorBlendState,
        .pDynamicState = &pDynamicState,
        .layout = RendererVulkan::get_instance()->bindless_pool->get_pipeline_layout(),
    });
    VK_CHECK(vkCreateGraphicsPipelines(r->dev, nullptr, 1, &vk_info, nullptr, &md->pipeline));
}

void VkPipelineMetadata::destroy(Pipeline& a)
{
    if(!a.metadata) { return; }
    auto* md = (VkPipelineMetadata*)a.metadata;
    assert(md->pipeline);
    vkDestroyPipeline(RendererVulkan::get_instance()->dev, md->pipeline, nullptr);
    delete a.metadata;
    a.metadata = nullptr;
}

VkPipelineMetadata& VkPipelineMetadata::get(Pipeline& a)
{
    assert(a.metadata);
    return *(VkPipelineMetadata*)a.metadata;
}

const VkPipelineMetadata& VkPipelineMetadata::get(const Pipeline& a)
{
    assert(a.metadata);
    return *(const VkPipelineMetadata*)a.metadata;
}

void VkBufferMetadata::init(Buffer& a)
{
    if(a.metadata)
    {
        ENG_ERROR("Trying to init already init buffer");
        return;
    }

    auto* md = new VkBufferMetadata{};
    a.metadata = md;
    const auto cpu_map = a.usage.test(gfx::BufferUsage::CPU_ACCESS);
    if(a.capacity == 0)
    {
        ENG_WARN("Capacity cannot be 0");
        return;
    }
    if(!cpu_map) { a.usage |= BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT; }

    auto* r = RendererVulkan::get_instance();
    const auto vkinfo = Vks(VkBufferCreateInfo{ .size = a.capacity, .usage = to_vk(a.usage) });
    const auto vmainfo = VmaAllocationCreateInfo{
        .flags = cpu_map ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = cpu_map ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0u
    };

    VmaAllocationInfo vmaai{};
    VK_CHECK(vmaCreateBuffer(r->vma, &vkinfo, &vmainfo, &md->buffer, &md->vmaa, &vmaai));
    if(md->buffer) { set_debug_name(md->buffer, a.name); }
    else
    {
        ENG_WARN("Could not create buffer {}", a.name);
        return;
    }
    a.memory = vmaai.pMappedData;
    if(vkinfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const auto vkbdai = Vks(VkBufferDeviceAddressInfo{ .buffer = md->buffer });
        md->bda = vkGetBufferDeviceAddress(r->dev, &vkbdai);
    }
}

void VkBufferMetadata::destroy(Buffer& a)
{
    if(!a.metadata)
    {
        assert(a.capacity == 0);
        return;
    }
    auto* r = RendererVulkan::get_instance();
    auto* md = (VkBufferMetadata*)a.metadata;
    if(!md->buffer || !md->vmaa) { return; }
    vmaDestroyBuffer(r->vma, md->buffer, md->vmaa);
    delete md;
    a.metadata = nullptr;
}

VkBufferMetadata& VkBufferMetadata::get(Buffer& a)
{
    assert(a.metadata);
    return *(VkBufferMetadata*)a.metadata;
}

const VkBufferMetadata& VkBufferMetadata::get(const Buffer& a)
{
    assert(a.metadata);
    return *(const VkBufferMetadata*)a.metadata;
}

void VkImageMetadata::init(Image& a, VkImage img)
{
    if(a.metadata)
    {
        ENG_ERROR("Trying to init already init image");
        return;
    }

    auto* r = RendererVulkan::get_instance();
    auto* md = new VkImageMetadata{};
    a.metadata = md;

    if(a.width + a.height + a.depth == 0)
    {
        ENG_WARN("Trying to create 0-sized image");
        return;
    }

    VmaAllocationCreateInfo vma_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    const auto info = Vks(VkImageCreateInfo{ .imageType = to_vk(a.type),
                                             .format = to_vk(a.format),
                                             .extent = { a.width, a.height, a.depth },
                                             .mipLevels = a.mips,
                                             .arrayLayers = a.layers,
                                             .samples = VK_SAMPLE_COUNT_1_BIT,
                                             .tiling = VK_IMAGE_TILING_OPTIMAL,
                                             .usage = to_vk(a.usage),
                                             .initialLayout = to_vk(a.current_layout) });
    if(img) { md->image = img; }
    else { VK_CHECK(vmaCreateImage(r->vma, &info, &vma_info, &md->image, &md->vmaa, nullptr)); }
    if(md->image) { set_debug_name(md->image, a.name); }
    else { ENG_ERROR("Could not create image {}", a.name); }
}

void VkImageMetadata::destroy(Image& a)
{
    if(!a.metadata) { return; }
    auto* r = RendererVulkan::get_instance();
    auto& md = VkImageMetadata::get(a);
    for(auto& e : md.views)
    {
        r->destroy_view(e);
    }
    vmaDestroyImage(r->vma, md.image, md.vmaa);
    delete a.metadata;
    a.metadata = nullptr;
}

VkImageMetadata& VkImageMetadata::get(Image& a)
{
    assert(a.metadata);
    return *(VkImageMetadata*)a.metadata;
}

void VkImageViewMetadata::init(ImageView& a, Handle<ImageView> handle, Image* img)
{
    if(a.metadata)
    {
        ENG_ERROR("Trying to init already init image view");
        return;
    }
    assert(a.image);
    auto* r = RendererVulkan::get_instance();
    if(!img) { img = &a.image.get(); }
    assert(img->metadata);
    const auto vkinfo = Vks(VkImageViewCreateInfo{
        .image = ((VkImageMetadata*)img->metadata)->image,
        .viewType = to_vk(a.type),
        .format = to_vk(a.format),
        .subresourceRange = { to_vk(a.aspect), a.mips.offset, a.mips.size, a.layers.offset, a.layers.size } });

    auto* md = new VkImageViewMetadata{};
    a.metadata = md;
    VK_CHECK(vkCreateImageView(r->dev, &vkinfo, {}, &md->view));
    if(!md->view) { ENG_ERROR("Could not create image view for image {}", img->name); }
    else
    {
        set_debug_name(md->view, a.name);
        VkImageMetadata::get(*img).views.push_back(handle);
    }
}

void VkImageViewMetadata::destroy(ImageView& a)
{
    if(!a.metadata) { return; }
    auto* md = (VkImageViewMetadata*)a.metadata;
    assert(md->view);
    auto* r = RendererVulkan::get_instance();
    vkDestroyImageView(r->dev, md->view, nullptr);
    delete md;
    a.metadata = nullptr;
}

VkImageViewMetadata& VkImageViewMetadata::get(ImageView& a)
{
    assert(a.metadata);
    return *(VkImageViewMetadata*)a.metadata;
}

// const VkImageViewMetadata& VkImageViewMetadata::get(const ImageView& a)
//{
//     assert(a.metadata);
//     return *(VkImageViewMetadata*)a.metadata;
// }

void VkSamplerMetadata::init(Sampler& a)
{
    if(a.metadata) { return; }
    auto vkinfo = Vks(VkSamplerCreateInfo{ .magFilter = gfx::to_vk(a.info.filtering[1]),
                                           .minFilter = gfx::to_vk(a.info.filtering[0]),
                                           .mipmapMode = gfx::to_vk(a.info.mipmap_mode),
                                           .addressModeU = gfx::to_vk(a.info.addressing[0]),
                                           .addressModeV = gfx::to_vk(a.info.addressing[1]),
                                           .addressModeW = gfx::to_vk(a.info.addressing[2]),
                                           .mipLodBias = a.info.mip_lod[2],
                                           .minLod = a.info.mip_lod[0],
                                           .maxLod = a.info.mip_lod[1] });
    auto vkreduction = Vks(VkSamplerReductionModeCreateInfo{});
    if(a.info.reduction_mode)
    {
        vkreduction.reductionMode = gfx::to_vk(*a.info.reduction_mode);
        vkinfo.pNext = &vkreduction;
    }
    auto* md = new VkSamplerMetadata{};
    a.metadata = md;
    VK_CHECK(vkCreateSampler(RendererVulkan::get_instance()->dev, &vkinfo, {}, &md->sampler));
}

void VkSamplerMetadata::destroy(Sampler& a)
{
    if(!a.metadata)
    {
        ENG_ERROR("Trying to init already init sampler.");
        return;
    }
    auto* md = (VkSamplerMetadata*)a.metadata;
    vkDestroySampler(RendererVulkan::get_instance()->dev, md->sampler, nullptr);
    delete md;
    a.metadata = nullptr;
}

VkSamplerMetadata& VkSamplerMetadata::get(Sampler& a)
{
    assert(a.metadata);
    return *(VkSamplerMetadata*)a.metadata;
}

// const VkSamplerMetadata& VkSamplerMetadata::get(const Sampler& a)
//{
//     assert(a.metadata);
//     return *(const VkSamplerMetadata*)a.metadata;
// }

RendererVulkan* RendererVulkan::get_instance() { return static_cast<RendererVulkan*>(Engine::get().renderer); }

void RendererVulkan::init()
{
    ENG_SET_HANDLE_DISPATCHER(gfx::Buffer, { return &gfx::RendererVulkan::get_instance()->buffers.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Image, { return &gfx::RendererVulkan::get_instance()->images.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::ImageView, { return &gfx::RendererVulkan::get_instance()->image_views.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Geometry, { return &gfx::RendererVulkan::get_instance()->geometries.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Mesh, { return &gfx::RendererVulkan::get_instance()->meshes.at(*handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Texture, { return &gfx::RendererVulkan::get_instance()->textures.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Material, { return &gfx::RendererVulkan::get_instance()->materials.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Shader, { return &gfx::RendererVulkan::get_instance()->shaders.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Pipeline, { return &gfx::RendererVulkan::get_instance()->pipelines.at(handle); });
    ENG_SET_HANDLE_DISPATCHER(gfx::Sampler, { return &gfx::RendererVulkan::get_instance()->samplers.at(handle); });

    initialize_vulkan();
    initialize_resources();
    initialize_mesh_passes();
    create_window_sized_resources();
    initialize_imgui();
    Engine::get().add_on_window_resize_callback([this] {
        on_window_resize();
        return true;
    });
    // flags.set(RenderFlags::REBUILD_RENDER_GRAPH);
}

void RendererVulkan::initialize_vulkan()
{
    // TODO: remove throws
    if(volkInitialize() != VK_SUCCESS) { throw std::runtime_error{ "Volk loader not found. Stopping" }; }

    vkb::InstanceBuilder builder;
    auto inst_ret = builder
                        .set_app_name("Example Vulkan Application")
#ifndef NDEBUG
                        .enable_validation_layers()
                        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                        .use_default_debug_messenger()
#endif
                        .require_api_version(VK_MAKE_API_VERSION(0, 1, 3, 0))
                        .build();

    if(!inst_ret) { throw std::runtime_error{ "Failed to create Vulkan instance. Error: " }; }

    vkb::Instance vkb_inst = inst_ret.value();
    volkLoadInstance(vkb_inst.instance);

    const auto* window = Engine::get().window;

    auto surface_info = Vks(VkWin32SurfaceCreateInfoKHR{
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = glfwGetWin32Window(window->window),
    });
    vkCreateWin32SurfaceKHR(vkb_inst.instance, &surface_info, nullptr, &window_surface);

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_rets = selector.require_present()
                         .set_surface(window_surface)
                         .set_minimum_version(1, 3)
                         .add_desired_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                         .add_desired_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                         .add_desired_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
                         .add_desired_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
                         .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)        // for imgui
                         .add_required_extension(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) // for imgui
                         .require_present()
                         .prefer_gpu_device_type()
                         .allow_any_gpu_device_type(false)
                         .select_devices();
    auto phys_ret = [&phys_rets] {
        for(auto& pd : *phys_rets)
        {
            if(pd.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { return pd; }
        }
        return *phys_rets->begin();
    }();
    if(!phys_ret) { throw std::runtime_error{ "Failed to select Vulkan Physical Device. Error: " }; }

    supports_raytracing = phys_ret.is_extension_present(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                          phys_ret.is_extension_present(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

    auto synch2_features = Vks(VkPhysicalDeviceSynchronization2Features{ .synchronization2 = true });

    auto dyn_features = Vks(VkPhysicalDeviceDynamicRenderingFeatures{ .dynamicRendering = true });

    auto dev_2_features = Vks(VkPhysicalDeviceFeatures2{ .features = {
                                                             .geometryShader = true,
                                                             .multiDrawIndirect = true,
                                                             .vertexPipelineStoresAndAtomics = true,
                                                             .fragmentStoresAndAtomics = true,
                                                         } });

    auto dev_vk12_features = Vks(VkPhysicalDeviceVulkan12Features{
        .drawIndirectCount = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .shaderStorageBufferArrayNonUniformIndexing = true,
        .shaderStorageImageArrayNonUniformIndexing = true,
        .descriptorBindingUniformBufferUpdateAfterBind = true,
        .descriptorBindingSampledImageUpdateAfterBind = true,
        .descriptorBindingStorageImageUpdateAfterBind = true,
        .descriptorBindingStorageBufferUpdateAfterBind = true,
        .descriptorBindingUpdateUnusedWhilePending = true,
        .descriptorBindingPartiallyBound = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .samplerFilterMinmax = true,
        .scalarBlockLayout = true,
        .hostQueryReset = true,
        .timelineSemaphore = true,
        .bufferDeviceAddress = true,
    });

    auto acc_features = Vks(VkPhysicalDeviceAccelerationStructureFeaturesKHR{
        .accelerationStructure = true,
        .descriptorBindingAccelerationStructureUpdateAfterBind = true,
    });

    auto rtpp_features = Vks(VkPhysicalDeviceRayTracingPipelineFeaturesKHR{
        .rayTracingPipeline = true,
        .rayTraversalPrimitiveCulling = true,
    });

    auto maint5_features = Vks(VkPhysicalDeviceMaintenance5FeaturesKHR{
        .maintenance5 = true,
    });

    auto rayq_features = Vks(VkPhysicalDeviceRayQueryFeaturesKHR{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = true,
    });

    rt_props = Vks(VkPhysicalDeviceRayTracingPipelinePropertiesKHR{});
    rt_acc_props = Vks(VkPhysicalDeviceAccelerationStructurePropertiesKHR{});
    vkb::DeviceBuilder device_builder{ phys_ret };
    device_builder.add_pNext(&dev_2_features).add_pNext(&dyn_features).add_pNext(&synch2_features).add_pNext(&dev_vk12_features);
    if(supports_raytracing)
    {
        device_builder.add_pNext(&acc_features).add_pNext(&rtpp_features).add_pNext(&rayq_features);
    }
    auto dev_ret = device_builder.build();
    if(!dev_ret) { throw std::runtime_error{ "Failed to create Vulkan device. Error: " }; }
    vkb::Device vkb_device = dev_ret.value();
    volkLoadDevice(vkb_device.device);

    VkDevice device = vkb_device.device;

    auto pdev_props = Vks(VkPhysicalDeviceProperties2{
        .pNext = &rt_props,
    });
    rt_props.pNext = &rt_acc_props;
    vkGetPhysicalDeviceProperties2(phys_ret.physical_device, &pdev_props);

    instance = vkb_inst.instance;
    dev = device;
    pdev = phys_ret.physical_device;
    submit_queue = new SubmitQueue{ dev, vkb_device.get_queue(vkb::QueueType::graphics).value(),
                                    vkb_device.get_queue_index(vkb::QueueType::graphics).value() };

    VmaVulkanFunctions vulkanFunctions = {
        .vkGetInstanceProcAddr = inst_ret->fp_vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = inst_ret->fp_vkGetDeviceProcAddr,
        .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory = vkAllocateMemory,
        .vkFreeMemory = vkFreeMemory,
        .vkMapMemory = vkMapMemory,
        .vkUnmapMemory = vkUnmapMemory,
        .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory = vkBindBufferMemory,
        .vkBindImageMemory = vkBindImageMemory,
        .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
        .vkCreateBuffer = vkCreateBuffer,
        .vkDestroyBuffer = vkDestroyBuffer,
        .vkCreateImage = vkCreateImage,
        .vkDestroyImage = vkDestroyImage,
        .vkCmdCopyBuffer = vkCmdCopyBuffer,
        .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
        .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
        .vkBindBufferMemory2KHR = vkBindBufferMemory2,
        .vkBindImageMemory2KHR = vkBindImageMemory2,
        .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
        .vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
        .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
    };

    VmaAllocatorCreateInfo allocatorCreateInfo = {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = pdev,
        .device = dev,
        .pVulkanFunctions = &vulkanFunctions,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &vma));
}

void RendererVulkan::initialize_imgui()
{
    imgui_renderer = new ImGuiRenderer{};
    imgui_renderer->initialize();
}

void RendererVulkan::initialize_resources()
{
    bindless_pool = new BindlessPool{ dev };
    staging_manager = new GPUStagingManager{};
    staging_manager->init(new SubmitQueue{ dev, submit_queue->queue, submit_queue->family_idx },
                          [](Handle<Buffer> buffer) { RendererVulkan::get_instance()->update_resource(buffer); });
    // vertex_positions_buffer = make_buffer(BufferCreateInfo{
    //     "vertex_positions_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
    //                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT });
    // vertex_attributes_buffer = make_buffer(BufferCreateInfo{
    //     "vertex_attributes_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT });
    // index_buffer =
    //     make_buffer(BufferCreateInfo{ "index_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
    //                                                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
    //                                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT });
    // meshlets_bs_buf = make_buffer(BufferCreateInfo{ "meshlets_bounding_spheres_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    // meshlets_mli_id_buf =
    //     make_buffer(BufferCreateInfo{ "meshlets_meshlest_instance_to_id_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });

    geom_main_bufs.buf_vpos = make_buffer(BufferDescriptor{ "vertex positions", 1024, BufferUsage::STORAGE_BIT });
    geom_main_bufs.buf_vattrs = make_buffer(BufferDescriptor{ "vertex attributes", 1024, BufferUsage::STORAGE_BIT });
    geom_main_bufs.buf_indices =
        make_buffer(BufferDescriptor{ "vertex indices", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDEX_BIT });
    geom_main_bufs.buf_draw_cmds = make_buffer(BufferDescriptor{
        "meshlets draw cmds", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT | BufferUsage::CPU_ACCESS });
    geom_main_bufs.buf_draw_ids = make_buffer(BufferDescriptor{ "meshlets instance id", 1024, BufferUsage::STORAGE_BIT });
    geom_main_bufs.buf_final_draw_ids =
        make_buffer(BufferDescriptor{ "meshlets final instance id", 1024, BufferUsage::STORAGE_BIT });
    geom_main_bufs.buf_draw_bs = make_buffer(BufferDescriptor{ "meshlets instance bbs", 1024, BufferUsage::STORAGE_BIT });

    // auto samp_ne = samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // auto samp_ll = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // auto samp_lr = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // ddgi.radiance_texture = make_image(Image{ "ddgi radiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
    //                                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                    ImageLayout::GENERAL);
    // make_image(ddgi.radiance_texture, ImageLayout::GENERAL, samp_ll);

    // ddgi.irradiance_texture = make_image(Image{ "ddgi irradiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
    //                                             VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                      ImageLayout::GENERAL);
    // make_image(ddgi.irradiance_texture, ImageLayout::GENERAL, samp_ll);

    // ddgi.visibility_texture = make_image(Image{ "ddgi visibility", 1, 1, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
    //                                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                      ImageLayout::GENERAL);
    // make_image(ddgi.visibility_texture, ImageLayout::GENERAL, samp_ll);

    // ddgi.probe_offsets_texture = make_image(Image{ "ddgi probe offsets", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
    //                                                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                         ImageLayout::GENERAL);
    // make_image(ddgi.probe_offsets_texture, ImageLayout::GENERAL, samp_ll);

    for(uint32_t i = 0; i < frame_datas.size(); ++i)
    {
        auto& fd = frame_datas[i];
        fd.cmdpool = submit_queue->make_command_pool();
        fd.acquire_semaphore = make_sync({ SyncType::BINARY_SEMAPHORE, 0, "acquire semaphore" });
        fd.rendering_semaphore = make_sync({ SyncType::BINARY_SEMAPHORE, 0, "rendering semaphore" });
        fd.rendering_fence = make_sync({ SyncType::FENCE, 1, "rendering fence" });
        fd.constants = make_buffer(BufferDescriptor{ fmt::format("constants_{}", i), 1024, BufferUsage::STORAGE_BIT });
    }

    // vsm.constants_buffer = make_buffer(BufferCreateInfo{ "vms buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    // vsm.free_allocs_buffer = make_buffer(BufferCreateInfo{ "vms alloc buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });

    // vsm.shadow_map_0 =
    //     make_image("vsm image", VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
    //                                                .format = VK_FORMAT_R32_SFLOAT,
    //                                                .extent = { VSM_PHYSICAL_PAGE_RESOLUTION, VSM_PHYSICAL_PAGE_RESOLUTION, 1 },
    //                                                .mipLevels = 1,
    //                                                .arrayLayers = 1,
    //                                                .samples = VK_SAMPLE_COUNT_1_BIT,
    //                                                .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
    //                                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT });

    // vsm.dir_light_page_table =
    //     make_image("vsm dir light 0 page table",
    //                VkImageCreateInfo{
    //                    .imageType = VK_IMAGE_TYPE_2D,
    //                    .format = VK_FORMAT_R32_UINT,
    //                    .extent = { VSM_VIRTUAL_PAGE_RESOLUTION, VSM_VIRTUAL_PAGE_RESOLUTION, 1 },
    //                    .mipLevels = 1,
    //                    .arrayLayers = VSM_NUM_CLIPMAPS,
    //                    .samples = VK_SAMPLE_COUNT_1_BIT,
    //                    .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    //                });

    // vsm.dir_light_page_table_rgb8 =
    //     make_image("vsm dir light 0 page table rgb8",
    //                VkImageCreateInfo{
    //                    .imageType = VK_IMAGE_TYPE_2D,
    //                    .format = VK_FORMAT_R8G8B8A8_UNORM,
    //                    .extent = { VSM_VIRTUAL_PAGE_RESOLUTION, VSM_VIRTUAL_PAGE_RESOLUTION, 1 },
    //                    .mipLevels = 1,
    //                    .arrayLayers = VSM_NUM_CLIPMAPS,
    //                    .samples = VK_SAMPLE_COUNT_1_BIT,
    //                    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    //                });

    // create default material
}

void RendererVulkan::initialize_mesh_passes()
{
    cull_pipeline = make_pipeline(PipelineCreateInfo{
        .shaders = { make_shader(ShaderStage::COMPUTE_BIT, "culling/culling.comp.glsl") } });
    hiz_pipeline = make_pipeline(PipelineCreateInfo{
        .shaders = { make_shader(ShaderStage::COMPUTE_BIT, "culling/hiz.comp.glsl") } });
    hiz_sampler = make_sampler(SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE },
        .mipmap_mode = SamplerMipmapMode::NEAREST,
        .reduction_mode = SamplerReductionMode::MIN });

    const auto pp_default_unlit = make_pipeline(PipelineCreateInfo{
        .shaders = { make_shader(ShaderStage::VERTEX_BIT, "default_unlit/unlit.vert.glsl"),
                     make_shader(ShaderStage::PIXEL_BIT, "default_unlit/unlit.frag.glsl") },
        .attachments = { .count = 1, .color_formats = { ImageFormat::R8G8B8A8_SRGB }, .depth_format = ImageFormat::D32_SFLOAT },
        .depth_test = true,
        .depth_write = true,
        .depth_compare = DepthCompare::GREATER,
        .culling = CullFace::BACK,
    });
    MeshPassCreateInfo info{ .name = "default_unlit" };
    info.effects[(uint32_t)MeshPassType::FORWARD] = make_shader_effect(ShaderEffect{ .pipeline = pp_default_unlit });
    default_meshpass = make_mesh_pass(info);
    default_material = materials.insert(Material{ .mesh_pass = default_meshpass }).handle;
}

void RendererVulkan::create_window_sized_resources()
{
    swapchain.create(dev, frame_datas.size(), Engine::get().window->width, Engine::get().window->height);

    for(auto i = 0ull; i < frame_datas.size(); ++i)
    {
        auto& fd = frame_datas.at(i);

        fd.hiz_pyramid = make_image(ImageDescriptor{
            .name = fmt::format("hiz_pyramid_{}", i),
            .width = (uint32_t)Engine::get().window->width,
            .height = (uint32_t)Engine::get().window->height,
            .mips = (uint32_t)std::log2f(std::max(Engine::get().window->width, Engine::get().window->height)) + 1,
            .format = ImageFormat::D32_SFLOAT,
            .usage = ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_SRC_BIT | ImageUsage::TRANSFER_DST_BIT });
        fd.hiz_debug_output =
            make_image(ImageDescriptor{ .name = fmt::format("hiz_debug_output_{}", i),
                                        .width = (uint32_t)Engine::get().window->width,
                                        .height = (uint32_t)Engine::get().window->height,
                                        .format = ImageFormat::R32FG32FB32FA32F,
                                        .usage = ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_RW });

        fd.gbuffer.color_image = make_image(ImageDescriptor{ .name = fmt::format("g_color_{}", i),
                                                             .width = (uint32_t)Engine::get().window->width,
                                                             .height = (uint32_t)Engine::get().window->height,
                                                             .format = ImageFormat::R8G8B8A8_SRGB,
                                                             .usage = ImageUsage::COLOR_ATTACHMENT_BIT |
                                                                      ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_RW });
        fd.gbuffer.depth_buffer_image = make_image(ImageDescriptor{
            .name = fmt::format("g_depth_{}", i),
            .width = (uint32_t)Engine::get().window->width,
            .height = (uint32_t)Engine::get().window->height,
            .format = ImageFormat::D32_SFLOAT,
            .usage = ImageUsage::DEPTH_STENCIL_ATTACHMENT_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_RW });

        /*make_image(&fd.gbuffer.color_image,
                     Image{ fmt::format("g_color_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                            VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
          make_image(&fd.gbuffer.view_space_positions_image,
                     Image{ fmt::format("g_view_pos_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                            VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });
          make_image(&fd.gbuffer.view_space_normals_image,
                     Image{ fmt::format("g_view_nor_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                            VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
          make_image(&fd.gbuffer.depth_buffer_image,
                     Image{ fmt::format("g_depth_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                            VK_FORMAT_D16_UNORM, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
          make_image(&fd.gbuffer.ambient_occlusion_image,
                     Image{ fmt::format("ao_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                            VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });*/
    }

    auto cmd = frame_datas[0].cmdpool->begin();
    for(auto i = 0ull; i < frame_datas.size(); ++i)
    {
        cmd->barrier(frame_datas[i].hiz_pyramid.get(), PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::ALL,
                     PipelineAccess::NONE, ImageLayout::UNDEFINED, ImageLayout::GENERAL);
        cmd->barrier(frame_datas[i].hiz_debug_output.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
                     PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW, ImageLayout::UNDEFINED, ImageLayout::GENERAL);
        auto& img = frame_datas[i].gbuffer.depth_buffer_image.get();
        cmd->clear_depth_stencil(img, ImageLayout::TRANSFER_DST, { 0, 1 }, { 0, 1 }, 0.0f, 0);
        cmd->barrier(img, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::EARLY_Z_BIT,
                     PipelineAccess::DS_RW, ImageLayout::TRANSFER_DST, ImageLayout::READ_ONLY);
        geom_main_bufs.transform_bufs[i] =
            make_buffer(BufferDescriptor{ fmt::format("transform_buffer_{}", i), 1024, BufferUsage::STORAGE_BIT });
    }
    frame_datas[0].cmdpool->end(cmd);
    submit_queue->with_cmd_buf(cmd).submit_wait(~0ull);
}

void RendererVulkan::build_render_graph()
{
    ENG_TODO();
    // rendergraph.clear_passes();
    //  rendergraph.add_pass<FFTOceanDebugGenH0Pass>(&rendergraph);
    //  rendergraph.add_pass<FFTOceanDebugGenHtPass>(&rendergraph);
    //  rendergraph.add_pass<FFTOceanDebugGenFourierPass>(&rendergraph);
    //  rendergraph.add_pass<FFTOceanDebugGenDisplacementPass>(&rendergraph);
    //  rendergraph.add_pass<FFTOceanDebugGenGradientPass>(&rendergraph);
    // rendergraph.add_pass<ZPrepassPass>(&rendergraph);
    //  rendergraph.add_pass<VsmClearPagesPass>(&rendergraph);
    //  rendergraph.add_pass<VsmPageAllocPass>(&rendergraph);
    //  rendergraph.add_pass<VsmShadowsPass>(&rendergraph);
    //  rendergraph.add_pass<VsmDebugPageCopyPass>(&rendergraph);
    // rendergraph.add_pass<DefaultUnlitPass>(&rendergraph);
    // rendergraph.add_pass<ImguiPass>(&rendergraph);
    // rendergraph.add_pass<SwapchainPresentPass>(&rendergraph);
    // rendergraph.bake();
}

void RendererVulkan::update()
{
    if(flags.test(RenderFlags::PAUSE_RENDERING)) { return; }
    if(flags.test_clear(RenderFlags::DIRTY_GEOMETRY_BATCHES_BIT))
    {
        // assert(false);
        //  upload_staged_models();
    }
    if(flags.test_clear(RenderFlags::DIRTY_MESH_INSTANCES))
    {
        // assert(false);
        //  upload_transforms();
    }
    if(flags.test_clear(RenderFlags::DIRTY_BLAS_BIT)) { build_blas(); }
    if(flags.test_clear(RenderFlags::DIRTY_TLAS_BIT))
    {
        build_tlas();
        update_ddgi();
        // TODO: prepare ddgi on scene update
    }
    if(flags.test_clear(RenderFlags::RESIZE_SWAPCHAIN_BIT))
    {
        submit_queue->wait_idle();
        create_window_sized_resources();
    }
    if(flags.test_clear(RenderFlags::REBUILD_RENDER_GRAPH))
    {
        assert(false);
        // build_render_graph();
        // pipelines.threaded_compile();
    }
    if(flags.test_clear(RenderFlags::UPDATE_BINDLESS_SET))
    {
        assert(false);
        submit_queue->wait_idle();
        // update_bindless_set();
    }
    if(!shaders_to_compile.empty()) { compile_shaders(); }
    if(!pipelines_to_compile.empty()) { compile_pipelines(); }

    auto& fd = get_frame_data();
    const auto frame_num = Engine::get().frame_num;
    fd.rendering_fence->wait_cpu(~0ull);
    fd.cmdpool->reset();

    uint32_t swapchain_index{};
    Image* swapchain_image{};
    {
        VkResult acquire_ret;
        swapchain_index = swapchain.acquire(&acquire_ret, ~0ull, fd.acquire_semaphore);
        if(acquire_ret != VK_SUCCESS)
        {
            ENG_WARN("Acquire image failed with: {}", static_cast<uint32_t>(acquire_ret));
            return;
        }
        swapchain_image = &swapchain.images[swapchain_index].get();
    }

    fd.rendering_fence->reset();

    static glm::mat4 s_view = Engine::get().camera->prev_view;
    if((glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
    {
        s_view = Engine::get().camera->prev_view;
    }

    {
        const float hx = (halton(Engine::get().frame_num % 4u, 2) * 2.0 - 1.0);
        const float hy = (halton(Engine::get().frame_num % 4u, 3) * 2.0 - 1.0);
        const glm::mat3 rand_mat =
            glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

        // const auto ldir = glm::normalize(*(glm::vec3*)Engine::get().scene->debug_dir_light_dir);
        // const auto cam = Engine::get().camera->pos;
        // auto eye = -ldir * 30.0f;
        // const auto lview = glm::lookAt(eye, eye + ldir, glm::vec3{ 0.0f, 1.0f, 0.0f });
        // const auto eyelpos = lview * glm::vec4{ cam, 1.0f };
        // const auto offset = glm::translate(glm::mat4{ 1.0f }, -glm::vec3{ eyelpos.x, eyelpos.y, 0.0f });
        // const auto dir_light_view = offset * lview;
        // const auto eyelpos2 = dir_light_view * glm::vec4{ cam, 1.0f };
        // ENG_LOG("CAMERA EYE DOT {} {}", eyelpos2.x, eyelpos2.y);
        //// const auto dir_light_proj = glm::perspectiveFov(glm::radians(90.0f), 8.0f * 1024.0f, 8.0f * 1024.0f, 0.0f, 150.0f);

        // GPUVsmConstantsBuffer vsmconsts{
        //     .dir_light_view = dir_light_view,
        //     .dir_light_dir = ldir,
        //     .num_pages_xy = VSM_VIRTUAL_PAGE_RESOLUTION,
        //     .max_clipmap_index = 0,
        //     .texel_resolution = float(VSM_PHYSICAL_PAGE_RESOLUTION),
        //     .num_frags = 0,
        // };

        // for(int i = 0; i < VSM_NUM_CLIPMAPS; ++i)
        //{
        //     float halfSize = float(VSM_CLIP0_LENGTH) * 0.5f * std::exp2f(float(i));
        //     float splitNear = (i == 0) ? 0.1f : float(VSM_CLIP0_LENGTH) * std::exp2f(float(i - 1));
        //     float splitFar = float(VSM_CLIP0_LENGTH) * std::exp2f(float(i));
        //     splitNear = 1.0;
        //     splitFar = 75.0;
        //     vsmconsts.dir_light_proj_view[i] =
        //         glm::ortho(-halfSize, +halfSize, -halfSize, +halfSize, splitNear, splitFar) * dir_light_view;
        // }

        GPUConstantsBuffer constants{
            .debug_view = s_view,
            .view = Engine::get().camera->get_view(),
            .proj = Engine::get().camera->get_projection(),
            .proj_view = Engine::get().camera->get_projection() * Engine::get().camera->get_view(),
            .inv_view = glm::inverse(Engine::get().camera->get_view()),
            .inv_proj = glm::inverse(Engine::get().camera->get_projection()),
            .inv_proj_view = glm::inverse(Engine::get().camera->get_projection() * Engine::get().camera->get_view()),
            .cam_pos = Engine::get().camera->pos,
        };
        staging_manager->copy(fd.constants, &constants, 0, { 0, sizeof(constants) });
        // staging_buffer->stage(vsm.constants_buffer, vsmconsts, 0ull);
    }

    if(flags.test_clear(RenderFlags::DIRTY_TRANSFORMS_BIT))
    {
        std::swap(geom_main_bufs.transform_bufs[0], geom_main_bufs.transform_bufs[1]);
    }

    uint32_t old_triangles = *((uint32_t*)geom_main_bufs.buf_draw_cmds->memory + 1);
    bake_indirect_commands();

    const auto cmd = fd.cmdpool->begin();
    // rendergraph.render(cmd);

    struct PushConstantsCulling
    {
        uint32_t constants_index;
        uint32_t ids_index;
        uint32_t post_cull_ids_index;
        uint32_t bs_index;
        uint32_t transforms_index;
        uint32_t indirect_commands_index;
        uint32_t hiz_source;
        uint32_t hiz_dest;
        uint32_t hiz_width;
        uint32_t hiz_height;
    };

    PushConstantsCulling push_constants_culling{ .constants_index = bindless_pool->get_index(fd.constants),
                                                 .ids_index = bindless_pool->get_index(geom_main_bufs.buf_draw_ids),
                                                 .post_cull_ids_index = bindless_pool->get_index(geom_main_bufs.buf_final_draw_ids),
                                                 .bs_index = bindless_pool->get_index(geom_main_bufs.buf_draw_bs),
                                                 .transforms_index = bindless_pool->get_index(geom_main_bufs.transform_bufs[0]),
                                                 .indirect_commands_index = bindless_pool->get_index(geom_main_bufs.buf_draw_cmds) };

    {
        auto& dep_image = fd.gbuffer.depth_buffer_image.get();
        auto& hiz_image = fd.hiz_pyramid.get();
        cmd->bind_pipeline(hiz_pipeline.get());
        if((glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
        {
            cmd->clear_depth_stencil(hiz_image, ImageLayout::GENERAL, { 0, VK_REMAINING_MIP_LEVELS }, { 0, 1 }, 0.0f, 0);
            cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                         PipelineAccess::SHADER_RW);
            cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE,
                         PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::READ_ONLY);

            push_constants_culling.hiz_width = hiz_image.width;
            push_constants_culling.hiz_height = hiz_image.height;

            bindless_pool->bind(cmd);
            for(auto i = 0u; i < hiz_image.mips; ++i)
            {
                if(i == 0)
                {
                    push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
                        make_view(ImageViewDescriptor{ .image = fd.gbuffer.depth_buffer_image, .aspect = ImageAspect::DEPTH }),
                        hiz_sampler, ImageLayout::READ_ONLY }));
                }
                else
                {
                    push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
                        make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { i - 1, 1 } }),
                        hiz_sampler, ImageLayout::GENERAL }));
                }
                push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
                    make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { i, 1 } }),
                    {},
                    ImageLayout::GENERAL }));
                push_constants_culling.hiz_width = std::max(hiz_image.width >> i, 1u);
                push_constants_culling.hiz_height = std::max(hiz_image.height >> i, 1u);
                cmd->push_constants(VK_SHADER_STAGE_ALL, &push_constants_culling, { 0, sizeof(push_constants_culling) });
                cmd->dispatch((push_constants_culling.hiz_width + 31) / 32, (push_constants_culling.hiz_height + 31) / 32, 1);
                cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                             PipelineAccess::SHADER_RW);
            }
        }
        else
        {
            bindless_pool->bind(cmd);
            cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE,
                         PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::READ_ONLY);
        }
        push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
            make_view(ImageViewDescriptor{
                .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { 0u, hiz_image.mips } }),
            hiz_sampler, ImageLayout::GENERAL }));
        push_constants_culling.hiz_dest = bindless_pool->get_index(make_texture(TextureDescriptor{
            make_view(ImageViewDescriptor{ .image = fd.hiz_debug_output, .aspect = ImageAspect::COLOR }), {}, ImageLayout::GENERAL }));
        cmd->clear_color(fd.hiz_debug_output.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
        cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
                     PipelineAccess::SHADER_RW);
        cmd->bind_pipeline(cull_pipeline.get());
        cmd->push_constants(VK_SHADER_STAGE_ALL, &push_constants_culling, { 0, sizeof(push_constants_culling) });
        cmd->dispatch((meshlet_instances.size() + 63) / 64, 1, 1);
        cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_WRITE_BIT, PipelineStage::INDIRECT_BIT,
                     PipelineAccess::INDIRECT_READ_BIT);
    }

    VkRenderingAttachmentInfo rainfos[]{
        Vks(VkRenderingAttachmentInfo{ .imageView = VkImageViewMetadata::get(swapchain_image->default_view.get()).view,
                                       .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                       .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                       .clearValue = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } } }),
        Vks(VkRenderingAttachmentInfo{ .imageView =
                                           VkImageViewMetadata::get(fd.gbuffer.depth_buffer_image->default_view.get()).view,
                                       .imageLayout = to_vk(ImageLayout::ATTACHMENT),
                                       .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                       .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0 } } })
    };
    const auto rinfo = Vks(VkRenderingInfo{ .renderArea = { { 0, 0 }, { swapchain_image->width, swapchain_image->height } },
                                            .layerCount = 1,
                                            .colorAttachmentCount = 1,
                                            .pColorAttachments = rainfos,
                                            .pDepthAttachment = &rainfos[1] });
    struct push_constants_1
    {
        uint32_t indices_index;
        uint32_t vertex_positions_index;
        uint32_t vertex_attributes_index;
        uint32_t transforms_index;
        uint32_t constants_index;
        uint32_t meshlet_instance_index;
        uint32_t meshlet_ids_index;
        uint32_t meshlet_bs_index;
        uint32_t hiz_pyramid_index;
        uint32_t hiz_debug_index;
    };
    push_constants_1 pc1{
        .indices_index = bindless_pool->get_index(geom_main_bufs.buf_indices),
        .vertex_positions_index = bindless_pool->get_index(geom_main_bufs.buf_vpos),
        .vertex_attributes_index = bindless_pool->get_index(geom_main_bufs.buf_vattrs),
        .transforms_index = bindless_pool->get_index(geom_main_bufs.transform_bufs[0]),
        .constants_index = bindless_pool->get_index(fd.constants),
        .meshlet_instance_index = bindless_pool->get_index(geom_main_bufs.buf_draw_ids),
        .meshlet_ids_index = bindless_pool->get_index(geom_main_bufs.buf_final_draw_ids),
        .meshlet_bs_index = bindless_pool->get_index(geom_main_bufs.buf_draw_bs),
        .hiz_pyramid_index = push_constants_culling.hiz_source,
        .hiz_debug_index = bindless_pool->get_index(make_texture(TextureDescriptor{
            make_view(ImageViewDescriptor{ .image = fd.hiz_debug_output

            }),
            make_sampler(SamplerDescriptor{ .mip_lod = { 0.0f, 1.0f, 0.0 } }), ImageLayout::READ_ONLY })),
    };

    cmd->bind_index(geom_main_bufs.buf_indices.get(), 0, VK_INDEX_TYPE_UINT16);
    cmd->barrier(*swapchain_image, PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::COLOR_OUT_BIT,
                 PipelineAccess::COLOR_WRITE_BIT, ImageLayout::UNDEFINED, ImageLayout::ATTACHMENT);
    cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE,
                 PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW, ImageLayout::UNDEFINED, ImageLayout::ATTACHMENT);
    cmd->begin_rendering(rinfo);

    VkViewport viewport{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
    VkRect2D scissor{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
    for(auto i = 0u, off = 0u; i < multibatches.size(); ++i)
    {
        const auto& mb = multibatches.at(i);
        const auto& p = pipelines.at(mb.pipeline);
        cmd->bind_pipeline(p);
        if(i == 0) { bindless_pool->bind(cmd); }
        cmd->push_constants(VK_SHADER_STAGE_ALL, &pc1, { 0u, sizeof(pc1) });
        cmd->set_viewports(&viewport, 1);
        cmd->set_scissors(&scissor, 1);
        cmd->draw_indexed_indirect_count(geom_main_bufs.buf_draw_cmds.get(), 8, geom_main_bufs.buf_draw_cmds.get(), 0,
                                         geom_main_bufs.command_count, sizeof(DrawIndirectCommand));
    }
    cmd->end_rendering();

    imgui_renderer->render(cmd);

    cmd->barrier(*swapchain_image, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, PipelineStage::ALL,
                 PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::PRESENT);

    fd.cmdpool->end(cmd);
    submit_queue->with_cmd_buf(cmd)
        .wait_sync(staging_manager->flush(), PipelineStage::ALL)
        .wait_sync(fd.acquire_semaphore, PipelineStage::COLOR_OUT_BIT)
        .signal_sync(fd.rendering_semaphore, PipelineStage::ALL)
        .signal_sync(fd.rendering_fence)
        .submit();

    submit_queue->wait_sync(fd.rendering_semaphore, PipelineStage::ALL).present(&swapchain);
    if(!flags.empty()) { ENG_WARN("render flags not empty at the end of the frame: {:b}", flags.flags); }

    flags.clear();
    submit_queue->wait_idle();

    uint32_t new_triangles = *((uint32_t*)geom_main_bufs.buf_draw_cmds->memory + 2);
    ENG_LOG("NUM TRIANGLES (PRE | POST) {} | {}; DIFF: {}", old_triangles, new_triangles, new_triangles - old_triangles);
    return;
}

void RendererVulkan::on_window_resize()
{
    flags.set(RenderFlags::RESIZE_SWAPCHAIN_BIT);
    // set_screen(ScreenRect{ .w = Engine::get().window->width, .h = Engine::get().window->height });
}

Handle<Buffer> RendererVulkan::make_buffer(const BufferDescriptor& info)
{
    auto handle = buffers.emplace(info);
    VkBufferMetadata::init(handle.get());
    return handle;
}

Handle<Image> RendererVulkan::make_image(const ImageDescriptor& info)
{
    auto handle = images.emplace(info);
    auto& img = handle.get();
    VkImageMetadata::init(img);
    img.default_view = make_view(ImageViewDescriptor{ .name = fmt::format("{}_default", info.name), .image = handle });
    if(info.data.size_bytes()) { staging_manager->copy(handle, info.data.data(), ImageLayout::READ_ONLY); }
    return handle;
}

Handle<ImageView> RendererVulkan::make_view(const ImageViewDescriptor& info)
{
    assert(info.image);
    auto& img = Handle{ info.image }.get();
    auto it = image_views.insert(ImageView{ .name = info.name,
                                            .image = info.image,
                                            .type = info.view_type ? *info.view_type : img.deduce_view_type(),
                                            .format = info.format ? *info.format : img.format,
                                            .aspect = info.aspect ? *info.aspect : img.deduce_aspect(),
                                            .mips = info.mips,
                                            .layers = info.layers });
    if(it.success) { VkImageViewMetadata::init(it.handle.get(), it.handle); }
    return it.handle;
}

Handle<Sampler> RendererVulkan::make_sampler(const SamplerDescriptor& info)
{
    auto it = samplers.insert(Sampler{ info, nullptr });
    if(it.success) { VkSamplerMetadata::init(it.handle.get()); }
    return it.handle;
}

Handle<Texture> RendererVulkan::make_texture(const TextureDescriptor& batch)
{
    return textures.insert(Texture{ batch.view, batch.sampler, batch.layout }).handle;
}

Handle<Material> RendererVulkan::make_material(const MaterialDescriptor& desc)
{
    auto meshpass = mesh_passes.find(MeshPass{ .name = desc.mesh_pass });
    if(!meshpass) { meshpass = default_meshpass; }
    return materials.insert(Material{ .mesh_pass = meshpass, .base_color_texture = desc.base_color_texture }).handle;
}

Handle<Geometry> RendererVulkan::make_geometry(const GeometryDescriptor& batch)
{
    std::vector<Vertex> out_vertices;
    std::vector<uint16_t> out_indices;
    std::vector<Meshlet> out_meshlets;
    meshletize_geometry(batch, out_vertices, out_indices, out_meshlets);

    Geometry geometry{ .vertex_range = { geom_main_bufs.vertex_count, out_vertices.size() },
                       .index_range = { geom_main_bufs.index_count, out_indices.size() },
                       .meshlet_range = { meshlets.size(), out_meshlets.size() } };

    static constexpr auto VXATTRSIZE = sizeof(Vertex) - sizeof(Vertex::position);
    std::vector<glm::vec3> positions;
    std::vector<std::byte> attributes;
    positions.resize(out_vertices.size());
    attributes.resize(out_vertices.size() * VXATTRSIZE);
    for(auto i = 0ull; i < out_vertices.size(); ++i)
    {
        auto& v = out_vertices.at(i);
        positions[i] = v.position;
        memcpy(&attributes[i * VXATTRSIZE], reinterpret_cast<const std::byte*>(&v) + sizeof(Vertex::position), VXATTRSIZE);
    }

    staging_manager->copy(geom_main_bufs.buf_vpos, positions, STAGING_APPEND);
    staging_manager->copy(geom_main_bufs.buf_vattrs, attributes, STAGING_APPEND);
    staging_manager->copy(geom_main_bufs.buf_indices, out_indices, STAGING_APPEND);

    geom_main_bufs.vertex_count += positions.size();
    geom_main_bufs.index_count += out_indices.size();
    meshlets.insert(meshlets.end(), out_meshlets.begin(), out_meshlets.end());

    const auto handle = geometries.insert(std::move(geometry));
    flags.set(RenderFlags::DIRTY_GEOMETRY_BATCHES_BIT);

    // clang-format off
    ENG_LOG("Batching geometry: [VXS: {:.2f} KB, IXS: {:.2f} KB]", 
            static_cast<float>(batch.vertices.size_bytes()) / 1000.0f,
            static_cast<float>(batch.indices.size_bytes()) / 1000.0f);
    // clang-format on

    return handle.handle;
}

void RendererVulkan::meshletize_geometry(const GeometryDescriptor& batch, std::vector<gfx::Vertex>& out_vertices,
                                         std::vector<uint16_t>& out_indices, std::vector<Meshlet>& out_meshlets)
{
    static constexpr auto max_verts = 64u;
    static constexpr auto max_tris = 124u;
    static constexpr auto cone_weight = 0.0f;

    const auto& indices = batch.indices;
    const auto& vertices = batch.vertices;
    const auto max_meshlets = meshopt_buildMeshletsBound(indices.size(), max_verts, max_tris);
    std::vector<meshopt_Meshlet> meshlets(max_meshlets);
    std::vector<meshopt_Bounds> meshlets_bounds;
    std::vector<uint32_t> meshlets_verts(max_meshlets * max_verts);
    std::vector<uint8_t> meshlets_triangles(max_meshlets * max_tris * 3);

    const auto meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshlets_verts.data(), meshlets_triangles.data(),
                                                     indices.data(), indices.size(), &vertices[0].position.x,
                                                     vertices.size(), sizeof(vertices[0]), max_verts, max_tris, cone_weight);

    const auto& last_meshlet = meshlets.at(meshlet_count - 1);
    meshlets_verts.resize(last_meshlet.vertex_offset + last_meshlet.vertex_count);
    meshlets_triangles.resize(last_meshlet.triangle_offset + ((last_meshlet.triangle_count * 3 + 3) & ~3));
    meshlets.resize(meshlet_count);
    meshlets_bounds.reserve(meshlet_count);

    for(auto& m : meshlets)
    {
        meshopt_optimizeMeshlet(&meshlets_verts.at(m.vertex_offset), &meshlets_triangles.at(m.triangle_offset),
                                m.triangle_count, m.vertex_count);
        const auto mbounds =
            meshopt_computeMeshletBounds(&meshlets_verts.at(m.vertex_offset), &meshlets_triangles.at(m.triangle_offset),
                                         m.triangle_count, &vertices[0].position.x, vertices.size(), sizeof(vertices[0]));
        meshlets_bounds.push_back(mbounds);
    }

    out_vertices.resize(meshlets_verts.size());
    std::transform(meshlets_verts.begin(), meshlets_verts.end(), out_vertices.begin(),
                   [&vertices](uint32_t idx) { return vertices[idx]; });

    out_indices.resize(meshlets_triangles.size());
    std::transform(meshlets_triangles.begin(), meshlets_triangles.end(), out_indices.begin(),
                   [](auto idx) { return static_cast<uint16_t>(idx); });
    out_meshlets.resize(meshlet_count);
    for(auto i = 0u; i < meshlet_count; ++i)
    {
        const auto& m = meshlets.at(i);
        const auto& mb = meshlets_bounds.at(i);
        out_meshlets.at(i) = gfx::Meshlet{ .vertex_offset = m.vertex_offset,
                                           .vertex_count = m.vertex_count,
                                           .index_offset = m.triangle_offset,
                                           .index_count = m.triangle_count * 3,
                                           .bounding_sphere = glm::vec4{ mb.center[0], mb.center[1], mb.center[2], mb.radius } };
    }
}

Handle<Mesh> RendererVulkan::make_mesh(const MeshDescriptor& batch)
{
    auto& bm = meshes.emplace_back(Mesh{ .geometry = batch.geometry, .material = batch.material });
    if(!bm.material) { bm.material = default_material; }
    return Handle<Mesh>{ (uint32_t)meshes.size() - 1 };
}

Image& RendererVulkan::get_image(Handle<Image> image) { return image.get(); }

Handle<Mesh> RendererVulkan::instance_mesh(const InstanceSettings& settings)
{
    const auto* transform = Engine::get().ecs->get<ecs::Transform>(settings.entity);
    const auto* mr = Engine::get().ecs->get<ecs::MeshRenderer>(settings.entity);
    if(!transform) { ENG_ERROR("Instanced node {} doesn't have transform component", settings.entity); }
    if(!mr) { return {}; }
    for(const auto& e : mr->meshes)
    {
        meshlets_to_instance.push_back(MeshletInstance{ .geometry = e->geometry, .material = e->material, .index = mesh_instance_index });
    }
    assert(entities.size() == mesh_instance_index);
    entities.push_back(settings.entity);
    if(!flags.test(RenderFlags::DIRTY_TRANSFORMS_BIT))
    {
        flags.set(RenderFlags::DIRTY_TRANSFORMS_BIT);
        // staging_manager->copy(geom_main_bufs.transform_bufs[1], geom_main_bufs.transform_bufs[0], 0,
        //                       { 0, geom_main_bufs.transform_bufs[0]->size });
        // staging_manager->flush();
        // staging_manager->wait_for_sync();
    }
    // staging_manager->copy(geom_main_bufs.transform_bufs[1], &transform->global, mesh_instance_index * sizeof(glm::mat4),
    //                       { 0, sizeof(glm::mat4) });
    return Handle<Mesh>{ mesh_instance_index++ };
}

void RendererVulkan::instance_blas(const BLASInstanceSettings& settings)
{
    ENG_TODO("Implement blas instancing");
    // auto& r = Engine::get().ecs_storage->get<components::Renderable>(settings.entity);
    // auto& mesh = meshes.at(r.mesh_handle);
    // auto& geometry = geometries.at(mesh.geometry);
    // auto& metadata = geometry_metadatas.at(geometry.metadata);
    // blas_instances.push_back(settings.entity);
    // flags.set(RenderFlags::DIRTY_TLAS_BIT);
    // if(!metadata.blas) {
    //     geometry.flags.set(GeometryFlags::DIRTY_BLAS_BIT);
    //     flags.set(RenderFlags::DIRTY_BLAS_BIT);
    // }
}

void RendererVulkan::update_transform(ecs::Entity entity)
{
    // update_positions.push_back(entity);
    flags.set(RenderFlags::DIRTY_TRANSFORMS_BIT);
}

size_t RendererVulkan::get_imgui_texture_id(Handle<Image> handle, ImageFilter filter, ImageAddressing addressing, uint32_t layer)
{
    return ~0ull;
    // struct ImguiTextureId
    //{
    //     ImTextureID id;
    //     VkImage image;
    //     ImageFilter filter;
    //     ImageAddressing addressing;
    //     uint32_t layer;
    // };
    // static std::unordered_multimap<Handle<Image>, ImguiTextureId> tex_ids;
    // auto range = tex_ids.equal_range(handle);
    // auto delete_it = tex_ids.end();
    // for(auto it = range.first; it != range.second; ++it)
    //{
    //     if(it->second.filter == filter && it->second.addressing == addressing && it->second.layer == layer)
    //     {
    //         if(it->second.image != handle->image)
    //         {
    //             ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(it->second.id));
    //             delete_it = it;
    //             break;
    //         }
    //         return it->second.id;
    //     }
    // }
    // if(delete_it != tex_ids.end()) { tex_ids.erase(delete_it); }
    // ImguiTextureId id{
    //     .id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(
    //         samplers.get_sampler(filter, addressing),
    //         handle->get_image_view(ImageViewDescriptor{ .name = "imgui_view", .mips = { 0, 1 }, .layers = { 0, 1 }
    //         }), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL)),
    //     .image = handle->image,
    //     .filter = filter,
    //     .addressing = addressing,
    //     .layer = layer,
    // };
    // tex_ids.emplace(handle, id);
    // return id.id;
}

Handle<Image> RendererVulkan::get_color_output_texture() const { return get_frame_data().gbuffer.color_image; }

void RendererVulkan::compile_shaders()
{
    std::chrono::high_resolution_clock::duration total_reading{};
    std::chrono::high_resolution_clock::duration total_hashing{};
    std::chrono::high_resolution_clock::duration total_compiling{};
    for(auto& e : shaders_to_compile)
    {
        auto& shader = e.get();
        shader.metadata = new ShaderMetadata{};
        ShaderMetadata* shmd = (ShaderMetadata*)shader.metadata;
        static const auto read_file = [](const std::filesystem::path& file_path) {
            std::string file_path_str = file_path.string();
            std::string include_paths = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders").string();
            char stbi_error[256] = {};
            char* stb_include_cstr = stb_include_file(file_path_str.data(), nullptr, include_paths.data(), stbi_error);
            if(!stb_include_cstr)
            {
                ENG_WARN("STBI_INCLUDE cannot parse file [{}]: {}", file_path_str, stbi_error);
                return std::string{};
            }
            std::string stb_include_str{ stb_include_cstr };
            free(stb_include_cstr);
            return stb_include_str;
        };

        const auto shckind = [stage = shader.stage] {
            if(stage == ShaderStage::VERTEX_BIT) { return shaderc_vertex_shader; }
            if(stage == ShaderStage::PIXEL_BIT) { return shaderc_fragment_shader; }
            // if(stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) { return shaderc_raygen_shader; }
            // if(stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) { return shaderc_closesthit_shader; }
            // if(stage == VK_SHADER_STAGE_MISS_BIT_KHR) { return shaderc_miss_shader; }
            if(stage == ShaderStage::COMPUTE_BIT) { return shaderc_compute_shader; }
            ENG_ERROR("Unrecognized shader type");
            return shaderc_vertex_shader;
        }();

        shaderc::CompileOptions shcopts;
        shcopts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        shcopts.SetTargetSpirv(shaderc_spirv_version_1_6);
        shcopts.SetGenerateDebugInfo();
        // shcopts.AddMacroDefinition("ASDF");

        auto t1 = std::chrono::high_resolution_clock::now();
        std::string shader_str = read_file(shader.path);
        auto t2 = std::chrono::high_resolution_clock::now();
        total_reading += (t2 - t1);
        t1 = std::chrono::high_resolution_clock::now();
        const auto shader_str_hash = eng::hash::combine_fnv1a(shader_str);
        t2 = std::chrono::high_resolution_clock::now();
        total_hashing += (t2 - t1);

        std::vector<uint32_t> out_spv;
        const auto pc_spv_path = std::filesystem::path{ shader.path.string() + ".precompiled" };
        std::fstream pc_spv_file{ pc_spv_path, std::fstream::binary | std::fstream::ate | std::fstream::in };
        if(pc_spv_file.is_open())
        {
            const size_t pc_spv_file_size = pc_spv_file.tellg();
            pc_spv_file.seekg(std::ios::beg);
            assert(pc_spv_file_size > 0);
            char pc_spv_hash_arr[8];
            pc_spv_file.read(pc_spv_hash_arr, 8);
            const auto pc_spv_hash = std::bit_cast<uint64_t>(pc_spv_hash_arr);
            if(pc_spv_hash == shader_str_hash)
            {
                out_spv.resize((pc_spv_file_size - sizeof(pc_spv_hash)) / sizeof(out_spv[0]));
                pc_spv_file.read(reinterpret_cast<char*>(out_spv.data()), pc_spv_file_size - 8);
            }
        }

        if(out_spv.empty())
        {
            shaderc::Compiler shccomp;
            t1 = std::chrono::high_resolution_clock::now();
            const auto res = shccomp.CompileGlslToSpv(shader_str, shckind, shader.path.filename().string().c_str(), shcopts);
            t2 = std::chrono::high_resolution_clock::now();
            total_compiling += (t2 - t1);
            if(res.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                ENG_WARN("Could not compile shader : {}, because : \"{}\"", shader.path.string(), res.GetErrorMessage());
                return;
            }
            out_spv = { res.begin(), res.end() };
            pc_spv_file.open(pc_spv_path, std::fstream::out | std::fstream::binary);
            char pc_spv_hash_arr[8];
            memcpy(pc_spv_hash_arr, &shader_str_hash, 8);
            pc_spv_file.write(pc_spv_hash_arr, 8);
            pc_spv_file.write(reinterpret_cast<const char*>(out_spv.data()), out_spv.size() * sizeof(out_spv[0]));
        }

        pc_spv_file.close();

        const auto module_info = Vks(VkShaderModuleCreateInfo{
            .codeSize = out_spv.size() * sizeof(uint32_t),
            .pCode = out_spv.data(),
        });
        VK_CHECK(vkCreateShaderModule(dev, &module_info, nullptr, &shmd->shader));
    }

    ENG_LOG("Compiling {} shader(s) finished. Parsing: {}ms, Hashing: {}ms, Compiling: {}ms", shaders_to_compile.size(),
            std::chrono::duration_cast<std::chrono::milliseconds>(total_reading).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(total_hashing).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(total_compiling).count());
    shaders_to_compile.clear();
}

void RendererVulkan::compile_pipelines()
{
    ENG_LOG("Compiling Pipelines");

    for(auto& e : pipelines_to_compile)
    {
        auto& p = e.get();
        const auto& info = p.info;
        VkPipelineMetadata::init(p);
    }

    pipelines_to_compile.clear();
}

void RendererVulkan::bake_indirect_commands()
{
    if(!meshlets_to_instance.empty())
    {
        for(const auto& e : meshlets_to_instance)
        {
            const auto& geom = e.geometry.get();
            meshlet_instances.reserve(meshlet_instances.size() + geom.meshlet_range.offset);
            for(auto i = 0u; i < geom.meshlet_range.size; ++i)
            {
                meshlet_instances.push_back(MeshletInstance{ .geometry = e.geometry,
                                                             .material = e.material,
                                                             .global_meshlet = (uint32_t)geom.meshlet_range.offset + i,
                                                             .index = e.index });
            }
        }

        meshlets_to_instance.clear();

        std::sort(meshlet_instances.begin(), meshlet_instances.end(), [this](const auto& a, const auto& b) {
            if(a.material >= b.material) { return false; }             // first sort by material
            if(a.global_meshlet >= b.global_meshlet) { return false; } // then sort by geometry
            return true;
        });
    }

    auto* ecsr = Engine::get().ecs;

    std::vector<DrawIndirectCommand> gpu_cmds(meshlet_instances.size());
    std::vector<GPUInstanceId> gpu_ids(meshlet_instances.size());
    multibatches.clear();
    multibatches.resize(meshlet_instances.size());
    Handle<Pipeline> prev_pipeline;
    uint32_t prev_meshlet = ~0u;
    int64_t cmd_off = -1;
    int64_t pp_off = -1;
    for(auto i = 0u; i < meshlet_instances.size(); ++i)
    {
        const auto& mi = meshlet_instances.at(i);
        const auto& mp = mesh_passes.at(mi.material->mesh_pass);
        const auto& pipeline = shader_effects.at(mp.effects[(uint32_t)MeshPassType::FORWARD]).pipeline;

        // if material changes (range of draw indirect commands that can be drawn with the same pipeline)
        if(prev_pipeline != pipeline)
        {
            prev_pipeline = pipeline;
            ++pp_off;
            multibatches.at(pp_off).pipeline = pipeline;
        }

        // if geometry changes, make new command
        if(prev_meshlet != mi.global_meshlet)
        {
            const auto& g = mi.geometry.get();
            const auto& ml = meshlets.at(mi.global_meshlet);
            prev_meshlet = mi.global_meshlet;
            ++cmd_off;
            gpu_cmds.at(cmd_off) = DrawIndirectCommand{
                .indexCount = ml.index_count,
                .instanceCount = 0,
                .firstIndex = (uint32_t)g.index_range.offset + ml.index_offset,
                .vertexOffset = (int32_t)(g.vertex_range.offset + ml.vertex_offset),
                .firstInstance = i,
            };
        }

        ++multibatches.at(pp_off).count;
        gpu_ids.at(i) = { (uint32_t)cmd_off, ~0ul, ~0ul };
    }
    gpu_cmds.resize(cmd_off + 1);
    multibatches.resize(pp_off + 1);
    geom_main_bufs.command_count = cmd_off + 1;

    std::vector<glm::vec4> gpu_bbs(meshlet_instances.size());
    for(auto i = 0u; i < meshlet_instances.size(); ++i)
    {
        // todo: use transform
        const auto& e = meshlet_instances.at(i);
        gpu_bbs.at(i) = meshlets.at(e.global_meshlet).bounding_sphere;
    }

    const auto gpu_cmd_count = (uint32_t)gpu_cmds.size();
    const auto post_cull_tri_count = 0u;
    const auto meshlet_instance_count = (uint32_t)meshlet_instances.size();
    staging_manager->copy(geom_main_bufs.buf_draw_cmds, &gpu_cmd_count, 0, { 0, 4 });
    staging_manager->copy(geom_main_bufs.buf_draw_cmds, &post_cull_tri_count, 4, { 0, 4 });
    staging_manager->copy(geom_main_bufs.buf_draw_cmds, gpu_cmds, 8);
    staging_manager->copy(geom_main_bufs.buf_draw_ids, &meshlet_instance_count, 0, { 0, 4 });
    staging_manager->copy(geom_main_bufs.buf_draw_ids, gpu_ids, 8);
    staging_manager->resize(geom_main_bufs.buf_final_draw_ids, meshlet_instances.size() * 4);
    staging_manager->copy(geom_main_bufs.buf_draw_bs, gpu_bbs, 0);
}

void RendererVulkan::build_blas()
{
    ENG_TODO("IMPLEMENT BACK");
    return;
#if 0
    auto triangles = Vks(VkAccelerationStructureGeometryTrianglesDataKHR{
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = { .deviceAddress = get_buffer(vertex_positions_buffer).bda },
        .vertexStride = sizeof(glm::vec3),
        .maxVertex = get_total_vertices() - 1u,
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData = { .deviceAddress = get_buffer(index_buffer).bda },
    });

    std::vector<const Geometry*> dirty_batches;
    std::vector<VkAccelerationStructureGeometryKHR> blas_geos;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> blas_geo_build_infos;
    std::vector<uint32_t> scratch_sizes;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    Handle<Buffer> scratch_buffer;

    blas_geos.reserve(geometries.size());

    for(auto& geometry : geometries) {
        if(!geometry.flags.test_clear(GeometryFlags::DIRTY_BLAS_BIT)) { continue; }
        GeometryMetadata& meta = geometry_metadatas.at(geometry.metadata);
        dirty_batches.push_back(&geometry);

        VkAccelerationStructureGeometryKHR& blas_geo = blas_geos.emplace_back();
        blas_geo = Vks(VkAccelerationStructureGeometryKHR{
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = { .triangles = triangles },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
        });

        VkAccelerationStructureBuildGeometryInfoKHR& build_geometry = blas_geo_build_infos.emplace_back();
        build_geometry = Vks(VkAccelerationStructureBuildGeometryInfoKHR{
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .geometryCount = 1,
            .pGeometries = &blas_geo,
        });

        const uint32_t primitive_count = geometry.index_count / 3u;
        auto build_size_info = Vks(VkAccelerationStructureBuildSizesInfoKHR{});
        vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_geometry,
                                                &primitive_count, &build_size_info);

        meta.blas_buffer =
            make_buffer("blas_buffer", build_size_info.accelerationStructureSize,
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        scratch_sizes.push_back(align_up2(build_size_info.buildScratchSize,
                                         static_cast<VkDeviceSize>(rt_acc_props.minAccelerationStructureScratchOffsetAlignment)));

        auto blas_info = Vks(VkAccelerationStructureCreateInfoKHR{
            .buffer = get_buffer(meta.blas_buffer).buffer,
            .size = build_size_info.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        });
        VK_CHECK(vkCreateAccelerationStructureKHR(dev, &blas_info, nullptr, &meta.blas));
    }

    // TODO: make non bindless buffer
    const auto total_scratch_size = std::accumulate(scratch_sizes.begin(), scratch_sizes.end(), 0ul);
    scratch_buffer = make_buffer("blas_scratch_buffer", total_scratch_size,
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false,
                                 rt_acc_props.minAccelerationStructureScratchOffsetAlignment);

    for(uint32_t i = 0, scratch_offset = 0; const auto& acc_geoms : blas_geos) {
        const Geometry& geom = *dirty_batches.at(i);
        const GeometryMetadata& meta = geometry_metadatas.at(geom.metadata);
        blas_geo_build_infos.at(i).scratchData.deviceAddress = get_buffer(scratch_buffer).bda + scratch_offset;
        blas_geo_build_infos.at(i).dstAccelerationStructure = meta.blas;

        VkAccelerationStructureBuildRangeInfoKHR& range_info = ranges.emplace_back();
        range_info = Vks(VkAccelerationStructureBuildRangeInfoKHR{
            .primitiveCount = geom.index_count / 3u,
            .primitiveOffset = (uint32_t)((geom.index_offset) * sizeof(uint32_t)),
            .firstVertex = geom.vertex_offset,
            .transformOffset = 0,
        });
        scratch_offset += scratch_sizes.at(i);
        ++i;
    }

    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> poffsets(ranges.size());
    for(uint32_t i = 0; i < ranges.size(); ++i) {
        poffsets.at(i) = &ranges.at(i);
    }

    auto cmd = get_frame_data().cmdpool->begin_onetime();
    vkCmdBuildAccelerationStructuresKHR(cmd, blas_geo_build_infos.size(), blas_geo_build_infos.data(), poffsets.data());
    get_frame_data().cmdpool->end(cmd);
    Sync f{ dev, false };
    gq.submit(cmd, &f);
    f.wait_cpu();
#endif
}

void RendererVulkan::build_tlas()
{
    return;
    // std::vector<uint32_t> tlas_mesh_offsets;
    // std::vector<uint32_t> blas_mesh_offsets;
    // std::vector<uint32_t> triangle_geo_inst_ids;
    // std::vector<VkAccelerationStructureInstanceKHR> tlas_instances;

    // std::sort(blas_instances.begin(), blas_instances.end(), [](auto a, auto b) {
    //     const auto& ra = Engine::get().ecs_storage->get<components::Renderable>(a);
    //     const auto& rb = Engine::get().ecs_storage->get<components::Renderable>(b);
    //     return ra.mesh_handle < rb.mesh_handle;
    // });

    assert(false);

// TODO: Compress mesh ids per triangle for identical blases with identical materials
// TODO: Remove geometry offset for indexing in shaders as all blases have only one geometry always
#if 0
    for(uint32_t i = 0, toff = 0, boff = 0; i < blas_instances.size(); ++i) {
        const uint32_t mi_idx = mesh_instance_idxs.at(blas_instances.at(i));
        const auto& mr = Engine::get().ecs_storage->get<components::Renderable>(mesh_instances.at(mi_idx));
        const Mesh& mb = meshes.at(mr.mesh_handle);
        const Geometry& geom = geometries.at(mb.geometry);
        const MeshMetadata& mm = mesh_metadatas.at(mb.metadata);
        /*std::distance(mesh_instances.begin(),
                      std::find_if(mesh_instances.begin(), mesh_instances.end(),
                                   [&bi](const RenderInstance& e) { return e.handle == bi.instance_handle; }));*/

        triangle_geo_inst_ids.reserve(triangle_geo_inst_ids.size() + geom.index_count / 3u);
        for(uint32_t j = 0; j < geom.index_count / 3u; ++j) {
            triangle_geo_inst_ids.push_back(mi_idx);
        }

        VkAccelerationStructureInstanceKHR& tlas_instance = tlas_instances.emplace_back();
        tlas_instance = Vks(VkAccelerationStructureInstanceKHR{
            .transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(glm::mat4x3{
                Engine::get().ecs_storage->get<components::Transform>(mesh_instances.at(mi_idx)).transform })),
            .instanceCustomIndex = 0,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference = geometry_metadatas.at(geom.metadata).blas_buffer.bda,
        });
        tlas_mesh_offsets.push_back(toff);
        blas_mesh_offsets.push_back(boff / 3u);
        ++toff;
        boff += geom.index_count; // TODO: validate this
    }
    // tlas_mesh_offsets_buffer = Buffer{ "tlas mesh offsets", tlas_mesh_offsets.size() * sizeof(tlas_mesh_offsets[0]),
    //                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    // tlas_mesh_offsets_buffer.push_data(tlas_mesh_offsets);
    // blas_mesh_offsets_buffer = Buffer{ "blas mesh offsets", blas_mesh_offsets.size() * sizeof(blas_mesh_offsets[0]),
    //                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    // blas_mesh_offsets_buffer.push_data(blas_mesh_offsets);
    // triangle_geo_inst_id_buffer =
    //     Buffer{ "triangle geo inst id", triangle_geo_inst_ids.size() * sizeof(triangle_geo_inst_ids[0]),
    //             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    // triangle_geo_inst_id_buffer.push_data(triangle_geo_inst_ids);
    // tlas_instance_buffer =
    //     Buffer{ "tlas_instance_buffer", sizeof(tlas_instances[0]) * tlas_instances.size(), 16u,
    //             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
    //             false };
    // tlas_instance_buffer.push_data(tlas_instances);

    auto geometry = Vks(VkAccelerationStructureGeometryKHR{
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = { .instances = Vks(VkAccelerationStructureGeometryInstancesDataKHR{
                          .arrayOfPointers = false,
                          .data = { .deviceAddress = tlas_instance_buffer.bda },
                      }) },
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    });

    auto tlas_info = Vks(VkAccelerationStructureBuildGeometryInfoKHR{
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1, // must be 1 for TLAS
        .pGeometries = &geometry,
    });

    auto build_size = Vks(VkAccelerationStructureBuildSizesInfoKHR{});
    const uint32_t max_primitives = tlas_instances.size();
    vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_info,
                                            &max_primitives, &build_size);

    /*  tlas_buffer =
          Buffer{ "tlas_buffer", build_size.accelerationStructureSize,
                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false };

      auto acc_info = Vks(VkAccelerationStructureCreateInfoKHR{
          .buffer = tlas_buffer.buffer,
          .size = build_size.accelerationStructureSize,
          .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
      });
      vkCreateAccelerationStructureKHR(dev, &acc_info, nullptr, &tlas);

      tlas_scratch_buffer = Buffer{ "tlas_scratch_buffer", build_size.buildScratchSize,
                                    rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

      tlas_info.dstAccelerationStructure = tlas;
      tlas_info.scratchData.deviceAddress = tlas_scratch_buffer.bda;

      auto build_range = Vks(VkAccelerationStructureBuildRangeInfoKHR{
          .primitiveCount = max_primitives,
          .primitiveOffset = 0,
          .firstVertex = 0,
          .transformOffset = 0,
      });
      VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };

      auto cmd = get_frame_data().cmdpool->begin_onetime();
      vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
      get_frame_data().cmdpool->end(cmd);
      Fence f{ dev, false };
      gq.submit(cmd, &f);
      f.wait();*/
#endif
}

void RendererVulkan::update_ddgi()
{
#if 0
    // assert(false && "no allocating new pointers for images");

    BoundingBox scene_aabb{ .min = glm::vec3{ -2.0f }, .max = glm::vec3{ 2.0f } };
    /*for(const Node& node : Engine::scene()->nodes) {
        if(!node.has_component<cmps::Mesh>()) { continue; }
        const cmps::Mesh& rm = Engine::ec()->get<cmps::Mesh>(node.handle);
        glm::mat4 t = Engine::scene()->get_final_transform(node.handle);
        BoundingBox m = rm.mesh->aabb;
        m.min = m.min * glm::mat4x3{ t };
        m.max = m.max * glm::mat4x3{ t };
        scene_aabb.min = glm::min(scene_aabb.min, m.min);
        scene_aabb.max = glm::max(scene_aabb.max, m.max);
    }*/

    ddgi.probe_dims = scene_aabb;
    const auto dim_scaling = glm::vec3{ 0.95, 0.7, 0.95 };
    ddgi.probe_dims.min *= dim_scaling;
    ddgi.probe_dims.max *= dim_scaling;
    ddgi.probe_distance = 1.5f;

    ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
    ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
                          std::bit_ceil(ddgi.probe_counts.z) };
    // ddgi.probe_counts = {4, 2, 4};
    const auto num_probes = ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z;

    ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };
    // ddgi.probe_walk = {ddgi.probe_walk.x, 4.0f, ddgi.probe_walk.z};

    const uint32_t irradiance_texture_width = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const uint32_t irradiance_texture_height = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.z;
    const uint32_t visibility_texture_width = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const uint32_t visibility_texture_height = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.z;

    *ddgi.radiance_texture = Image{ "ddgi radiance",
                                    ddgi.rays_per_probe,
                                    ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z,
                                    1,
                                    1,
                                    1,
                                    VK_FORMAT_R16G16B16A16_SFLOAT,
                                    VK_SAMPLE_COUNT_1_BIT,
                                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
    *ddgi.irradiance_texture = Image{
        "ddgi irradiance", irradiance_texture_width, irradiance_texture_height, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    };
    *ddgi.visibility_texture = Image{
        "ddgi visibility", visibility_texture_width, visibility_texture_height, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    };

    *ddgi.probe_offsets_texture = Image{ "ddgi probe offsets",
                                         ddgi.probe_counts.x * ddgi.probe_counts.y,
                                         ddgi.probe_counts.z,
                                         1,
                                         1,
                                         1,
                                         VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_SAMPLE_COUNT_1_BIT,
                                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };

    auto cmd = get_frame_data().cmdpool->begin_onetime();
    ddgi.radiance_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
                                             VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
    ddgi.irradiance_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
                                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                               VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
    ddgi.visibility_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
                                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                               VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
    ddgi.probe_offsets_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
                                                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
    get_frame_data().cmdpool->end(cmd);
    gq.submit(cmd);

    ddgi.buffer = Buffer{ "ddgi_settings_buffer", sizeof(DDGI::GPULayout),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };
    ddgi.debug_probe_offsets_buffer =
        Buffer{ "ddgi debug probe offsets buffer", sizeof(DDGI::GPUProbeOffsetsLayout) * num_probes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };

    DDGI::GPULayout ddgi_gpu_settings{
        .radiance_tex_size = glm::ivec2{ ddgi.radiance_texture->width, ddgi.radiance_texture->height },
        .irradiance_tex_size = glm::ivec2{ ddgi.irradiance_texture->width, ddgi.irradiance_texture->height },
        .visibility_tex_size = glm::ivec2{ ddgi.visibility_texture->width, ddgi.visibility_texture->height },
        .probe_offset_tex_size = glm::ivec2{ ddgi.probe_offsets_texture->width, ddgi.probe_offsets_texture->height },
        .probe_start = ddgi.probe_dims.min,
        .probe_counts = ddgi.probe_counts,
        .probe_walk = ddgi.probe_walk,
        .min_probe_distance = 0.01f,
        .max_probe_distance = 20.0f,
        .min_dist = 0.1f,
        .max_dist = 20.0f,
        .normal_bias = 0.08f,
        .max_probe_offset = 0.5f,
        .frame_num = 0,
        .irradiance_probe_side = ddgi.irradiance_probe_side,
        .visibility_probe_side = ddgi.visibility_probe_side,
        .rays_per_probe = ddgi.rays_per_probe,
        .debug_probe_offsets = ddgi.debug_probe_offsets_buffer.bda
    };

    if(ddgi.probe_counts.y == 1) {
        ddgi_gpu_settings.probe_start.y += ddgi.probe_walk.y * 0.5f;
        ddgi.probe_start.y += ddgi.probe_walk.y * 0.5f;
    }

    ddgi.buffer.push_data(&ddgi_gpu_settings, sizeof(DDGI::GPULayout), 0);
    submit_queue->wait_idle();
#endif
}

Handle<Shader> RendererVulkan::make_shader(ShaderStage stage, const std::filesystem::path& path)
{
    auto ret = shaders.insert(Shader{ .path = eng::paths::canonize_path(eng::paths::SHADERS_DIR / path), .stage = stage });
    if(ret.success) { shaders_to_compile.push_back(ret.handle); }
    return ret.handle;
}

Handle<Pipeline> RendererVulkan::make_pipeline(const PipelineCreateInfo& info)
{
    Pipeline p{ .info = info };
    auto ret = pipelines.insert(std::move(p));
    if(ret.success) { pipelines_to_compile.push_back(ret.handle); }
    return ret.handle;
}

Handle<ShaderEffect> RendererVulkan::make_shader_effect(const ShaderEffect& info)
{
    return shader_effects.insert(info).handle;
}

Handle<MeshPass> RendererVulkan::make_mesh_pass(const MeshPassCreateInfo& info)
{
    auto it = mesh_passes.insert(MeshPass{ .name = info.name, .effects = info.effects });
    return it.handle;
}

Sync* RendererVulkan::make_sync(const SyncCreateInfo& info)
{
    auto* s = syncs.emplace_back(new Sync{});
    s->init(info);
    return s;
}

void RendererVulkan::destroy_buffer(Handle<Buffer> buffer)
{
    VkBufferMetadata::destroy(buffer.get());
    buffers.erase(buffer);
}

void gfx::RendererVulkan::destroy_image(Handle<Image> image)
{
    VkImageMetadata::destroy(image.get());
    images.erase(image);
}

void RendererVulkan::destroy_view(Handle<ImageView> view)
{
    VkImageViewMetadata::destroy(view.get());
    image_views.erase(view);
}

uint32_t RendererVulkan::get_bindless(Handle<Buffer> buffer) { return bindless_pool->get_index(buffer); }

void RendererVulkan::update_resource(Handle<Buffer> dst) { bindless_pool->update_index(dst); }

FrameData& RendererVulkan::get_frame_data(uint32_t offset)
{
    return frame_datas[(Engine::get().frame_num + offset) % frame_datas.size()];
}

const FrameData& RendererVulkan::get_frame_data(uint32_t offset) const
{
    return const_cast<RendererVulkan*>(this)->get_frame_data();
}

void Swapchain::create(VkDevice dev, uint32_t image_count, uint32_t width, uint32_t height)
{
    const auto image_usage_flags = ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::TRANSFER_SRC_BIT | ImageUsage::TRANSFER_DST_BIT;
    const auto image_format = ImageFormat::R8G8B8A8_SRGB;
    auto* r = RendererVulkan::get_instance();
    auto sinfo = Vks(VkSwapchainCreateInfoKHR{
        // sinfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        // sinfo.pNext = &format_list_info;
        .surface = r->window_surface,
        .minImageCount = image_count,
        .imageFormat = to_vk(image_format),
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = VkExtent2D{ width, height },
        .imageArrayLayers = 1,
        .imageUsage = to_vk(image_usage_flags),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = true,
    });

    if(swapchain) { vkDestroySwapchainKHR(dev, swapchain, nullptr); }
    VK_CHECK(vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain));
    std::vector<VkImage> vkimgs(image_count);
    images.resize(image_count);
    views.resize(image_count);

    VK_CHECK(vkGetSwapchainImagesKHR(dev, swapchain, &image_count, vkimgs.data()));

    for(uint32_t i = 0; i < image_count; ++i)
    {
        Image img{};
        img.name = fmt::format("swapchain_image_{}", i);
        img.format = image_format;
        img.width = sinfo.imageExtent.width;
        img.height = sinfo.imageExtent.height;
        img.usage = image_usage_flags;
        VkImageMetadata::init(img, vkimgs.at(i));
        images[i] = r->images.insert(std::move(img));
        views[i] = r->make_view(ImageViewDescriptor{ .name = fmt::format("swapchain_view_{}", i), .image = images[i] });
        images[i]->default_view = views[i];
    }
}

uint32_t Swapchain::acquire(VkResult* res, uint64_t timeout, Sync* semaphore, Sync* fence)
{
    uint32_t idx;
    VkSemaphore vksem{};
    VkFence vkfen{};
    if(semaphore)
    {
        if(semaphore->type == SyncType::BINARY_SEMAPHORE) { vksem = semaphore->semaphore; }
        else
        {
            ENG_ERROR("Invalid sync type: {}", eng::to_string(semaphore->type));
            return ~0ull;
        }
    }
    if(fence)
    {
        if(fence->type == SyncType::FENCE) { vkfen = fence->fence; }
        else
        {
            ENG_ERROR("Invalid sync type: {}", eng::to_string(fence->type));
            return ~0ull;
        }
    }
    auto result = vkAcquireNextImageKHR(RendererVulkan::get_instance()->dev, swapchain, timeout, vksem, vkfen, &idx);
    if(res) { *res = result; }
    current_index = idx;
    return idx;
}

Image& Swapchain::get_current_image() { return images.at(current_index).get(); }

VkImageView& Swapchain::get_current_view() { return VkImageViewMetadata::get(views.at(current_index).get()).view; }

} // namespace gfx

} // namespace eng
