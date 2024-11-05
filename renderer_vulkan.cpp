#include <numeric>
#include <fstream>
#include <utility>
#include <volk/volk.h>
#include <glm/mat3x3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_core.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <vk-bootstrap/src/VkBootstrap.h>
#include <shaderc/shaderc.hpp>
#define STB_INCLUDE_IMPLEMENTATION
#define STB_INCLUDE_LINE_GLSL
#include <stb/stb_include.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"
#include "utils.hpp"

ENABLE_FLAGS_OPERATORS(RendererFlags)

// https://www.shadertoy.com/view/WlSSWc
static float halton(int i, int b) {
    /* Creates a halton sequence of values between 0 and 1.
        https://en.wikipedia.org/wiki/Halton_sequence
        Used for jittering based on a constant set of 2D points. */
    float f = 1.0;
    float r = 0.0;
    while(i > 0) {
        f = f / float(b);
        r = r + f * float(i % b);
        i = i / b;
    }

    return r;
}

PipelineLayout::PipelineLayout(std::array<DescriptorLayout, MAX_SETS> desc_layouts, u32 push_size)
    : sets(desc_layouts), push_size(push_size) {
    std::array<VkDescriptorSetLayout, MAX_SETS> vksets{};
    for(u32 i = 0; i < MAX_SETS; ++i) {
        auto& l = sets.at(i);
        vks::DescriptorSetLayoutCreateInfo info;
        std::array<VkDescriptorSetLayoutBinding, DescriptorLayout::MAX_BINDINGS> vkb{};
        std::array<VkDescriptorBindingFlags, DescriptorLayout::MAX_BINDINGS> bflags{};
        vks::DescriptorSetLayoutBindingFlagsCreateInfo bflags_info{};
        u32 bcount = 0;
        for(u32 j = 0; j < DescriptorLayout::MAX_BINDINGS; ++j) {
            DescriptorBinding& b = l.bindings[j];
            if(b.res.index() == 0) { continue; }
            auto deduced_type = b.get_vktype();
            vkb[bcount++] = { .binding = j, .descriptorType = deduced_type, .descriptorCount = b.count, .stageFlags = VK_SHADER_STAGE_ALL };
        }
        info.bindingCount = bcount;
        info.pBindings = vkb.data();
        info.pNext = &bflags_info;
        bflags_info.bindingCount = bcount;
        bflags_info.pBindingFlags = bflags.data();
        if(l.variable_binding != -1) {
            bflags[l.variable_binding] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        }

        VK_CHECK(vkCreateDescriptorSetLayout(get_renderer().dev, &info, nullptr, &vksets[i]));
        l.layout = vksets[i];
    }
    VkPushConstantRange range{ .stageFlags = VK_SHADER_STAGE_ALL, .offset = 0, .size = push_size };
    vks::PipelineLayoutCreateInfo info;
    info.setLayoutCount = vksets.size();
    info.pSetLayouts = vksets.data();
    info.pushConstantRangeCount = 1;
    info.pPushConstantRanges = &range;
    VK_CHECK(vkCreatePipelineLayout(get_renderer().dev, &info, nullptr, &layout));
}

Pipeline::Pipeline(const std::vector<VkShaderModule>& shaders, const PipelineLayout* layout,
                   std::variant<std::monostate, RasterizationSettings, RaytracingSettings> settings)
    : layout(layout) {
    std::vector<vks::PipelineShaderStageCreateInfo> stages;
    for(const auto& p : shaders) {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = get_renderer().shaders.metadatas.at(p).stage;
        stage.module = p;
        stage.pName = "main";
        // clang-format off
         if(stage.stage == VK_SHADER_STAGE_VERTEX_BIT) { type = Raster; }
         else if(stage.stage == VK_SHADER_STAGE_COMPUTE_BIT) { type = Compute; }
         else if(stage.stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) { type = RT; }
         stages.push_back(stage);
        // clang-format on
    }

    if(type == Raster) {
        if(settings.index() == 0) {
            rasterization_settings = RasterizationSettings{};
        } else {
            rasterization_settings = *std::get_if<RasterizationSettings>(&settings);
        }
        /*layout(location = 0) in vec3 pos;
        layout(location = 1) in vec3 nor;
        layout(location = 2) in vec2 uv;*/

        VkVertexInputAttributeDescription inputs[3]{
            { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
            { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, nor) },
            { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) }
        };
        VkVertexInputBindingDescription binding{ .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
        vks::PipelineVertexInputStateCreateInfo pVertexInputState;
        pVertexInputState.vertexAttributeDescriptionCount = sizeof(inputs) / sizeof(inputs[0]);
        pVertexInputState.pVertexAttributeDescriptions = inputs;
        pVertexInputState.vertexBindingDescriptionCount = 1;
        pVertexInputState.pVertexBindingDescriptions = &binding;

        vks::PipelineInputAssemblyStateCreateInfo pInputAssemblyState;
        pInputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pInputAssemblyState.primitiveRestartEnable = false;

        vks::PipelineTessellationStateCreateInfo pTessellationState;

        vks::PipelineViewportStateCreateInfo pViewportState;

        vks::PipelineRasterizationStateCreateInfo pRasterizationState;
        pRasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        pRasterizationState.cullMode = rasterization_settings.culling;
        pRasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pRasterizationState.lineWidth = 1.0f;

        vks::PipelineMultisampleStateCreateInfo pMultisampleState;
        pMultisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        vks::PipelineDepthStencilStateCreateInfo pDepthStencilState;
        pDepthStencilState.depthTestEnable = rasterization_settings.depth_test;
        pDepthStencilState.depthWriteEnable = true;
        pDepthStencilState.depthCompareOp = rasterization_settings.depth_op;
        pDepthStencilState.depthBoundsTestEnable = false;
        pDepthStencilState.stencilTestEnable = false;
        pDepthStencilState.front = {};
        pDepthStencilState.back = {};

        vks::PipelineColorBlendStateCreateInfo pColorBlendState;
        VkPipelineColorBlendAttachmentState blend1{ .colorWriteMask = 0b1111 /*RGBA*/ };
        pColorBlendState.attachmentCount = 1;
        pColorBlendState.pAttachments = &blend1;

        VkDynamicState dynstates[]{
            VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
            VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
        };
        vks::PipelineDynamicStateCreateInfo pDynamicState;
        pDynamicState.dynamicStateCount = sizeof(dynstates) / sizeof(dynstates[0]);
        pDynamicState.pDynamicStates = dynstates;

        vks::PipelineRenderingCreateInfo pDynamicRendering;
        pDynamicRendering.colorAttachmentCount = rasterization_settings.num_col_formats;
        pDynamicRendering.pColorAttachmentFormats = rasterization_settings.col_formats.data();
        pDynamicRendering.depthAttachmentFormat = rasterization_settings.dep_format;
        // pDynamicRendering.stencilAttachmentFormat = rasterization_settings.st_format;

        vks::GraphicsPipelineCreateInfo info;
        info.stageCount = stages.size();
        info.pStages = stages.data();
        info.pVertexInputState = &pVertexInputState;
        info.pInputAssemblyState = &pInputAssemblyState;
        info.pTessellationState = &pTessellationState;
        info.pViewportState = &pViewportState;
        info.pRasterizationState = &pRasterizationState;
        info.pMultisampleState = &pMultisampleState;
        info.pDepthStencilState = &pDepthStencilState;
        info.pColorBlendState = &pColorBlendState;
        info.pDynamicState = &pDynamicState;
        info.layout = layout->layout;
        info.pNext = &pDynamicRendering;

        VK_CHECK(vkCreateGraphicsPipelines(get_renderer().dev, nullptr, 1, &info, nullptr, &pipeline));
    } else if(type == Compute) {
        assert(stages.size() == 1);
        vks::ComputePipelineCreateInfo info;
        info.stage = stages.at(0);
        info.layout = layout->layout;
        VK_CHECK(vkCreateComputePipelines(get_renderer().dev, nullptr, 1, &info, nullptr, &pipeline));
    } else if(type == RT) {
        if(settings.index() == 0) {
            raytracing_settings = RaytracingSettings{};
        } else {
            raytracing_settings = *std::get_if<RaytracingSettings>(&settings);
        }

        std::vector<vks::RayTracingShaderGroupCreateInfoKHR> shader_groups;
        shader_groups.reserve(stages.size());
        std::queue<vks::RayTracingShaderGroupCreateInfoKHR*> incomplete;

        for(u32 i = 0; i < stages.size(); ++i) {
            auto& s = stages.at(i);
            vks::RayTracingShaderGroupCreateInfoKHR to_add{};
            to_add.generalShader = VK_SHADER_UNUSED_KHR;
            to_add.closestHitShader = VK_SHADER_UNUSED_KHR;
            to_add.anyHitShader = VK_SHADER_UNUSED_KHR;
            to_add.intersectionShader = VK_SHADER_UNUSED_KHR;
            if(s.stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR || s.stage == VK_SHADER_STAGE_MISS_BIT_KHR) {
                to_add.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                to_add.generalShader = i;
                shader_groups.push_back(to_add);
            } else {
                enum Add { Dont, Closest, Any, Inter };
                Add add{ Dont };

                if(!incomplete.empty()) {
                    auto& f = incomplete.front();
                    if(s.stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) {
                        if(f->closestHitShader == VK_SHADER_UNUSED_KHR) {
                            f->closestHitShader = i;
                        } else {
                            add = Closest;
                        }
                    } else if(s.stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR) {
                        assert(false);
                    } else if(s.stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) {
                        assert(false);
                    } else {
                        assert(false);
                    }

                    if(f->type == VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR &&
                       f->closestHitShader != VK_SHADER_UNUSED_KHR && f->anyHitShader != VK_SHADER_UNUSED_KHR) {
                        incomplete.pop();
                    } else if(f->type == VK_SHADER_GROUP_SHADER_INTERSECTION_KHR && f->closestHitShader != VK_SHADER_UNUSED_KHR &&
                              f->anyHitShader != VK_SHADER_UNUSED_KHR && f->intersectionShader != VK_SHADER_UNUSED_KHR) {
                        incomplete.pop();
                    }
                } else {
                    if(s.stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) {
                        add = Closest;
                    } else if(s.stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR) {
                        add = Any;
                    } else if(s.stage == VK_SHADER_STAGE_INTERSECTION_BIT_KHR) {
                        add = Inter;
                    }
                }

                if(add == Closest) {
                    to_add.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                    to_add.closestHitShader = i;
                } else if(add == Any) {
                    to_add.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                    to_add.anyHitShader = i;
                } else if(add == Inter) {
                    to_add.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
                    to_add.intersectionShader = i;
                }

                if(add != Dont) {
                    shader_groups.push_back(to_add);
                    incomplete.push(&shader_groups.back());
                }
            }
        }
        vks::RayTracingPipelineCreateInfoKHR info;
        info.stageCount = stages.size();
        info.pStages = stages.data();
        info.groupCount = shader_groups.size();
        info.pGroups = shader_groups.data();
        info.maxPipelineRayRecursionDepth = raytracing_settings.recursion_depth;
        info.layout = layout->layout;
        VK_CHECK(vkCreateRayTracingPipelinesKHR(get_renderer().dev, nullptr, nullptr, 1, &info, nullptr, &pipeline));

        raytracing_settings.group_count = shader_groups.size();
        const u32 handleSize = get_renderer().rt_props.shaderGroupHandleSize;
        const u32 handleSizeAligned =
            align_up(get_renderer().rt_props.shaderGroupHandleSize, get_renderer().rt_props.shaderGroupHandleAlignment);
        const u32 groupCount = static_cast<u32>(shader_groups.size());
        const u32 sbtSize = groupCount * handleSizeAligned;

        std::vector<u8> shaderHandleStorage(sbtSize);
        vkGetRayTracingShaderGroupHandlesKHR(get_renderer().dev, pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

        const VkBufferUsageFlags bufferUsageFlags =
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        raytracing_settings.sbt = get_renderer().make_buffer(Buffer{ "buffer_sbt", sbtSize, bufferUsageFlags, false });
        raytracing_settings.sbt->push_data(shaderHandleStorage);
    }
}

VkPipelineBindPoint Pipeline::get_bindpoint() const {
    switch(type) {
    case Pipeline::Raster:
        return VK_PIPELINE_BIND_POINT_GRAPHICS;
    case Pipeline::Compute:
        return VK_PIPELINE_BIND_POINT_COMPUTE;
    case Pipeline::RT:
        return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    default:
        assert(false);
        return VK_PIPELINE_BIND_POINT_MAX_ENUM;
    }
}

void RendererVulkan::init() {
    initialize_vulkan();
    // create_rt_output_image();
    //  compile_shaders();
    // build_pipelines();
    // build_sbt();
    initialize_resources();
    initialize_imgui();
    /*build_tlas();
    prepare_ddgi();*/
    Engine::add_on_window_resize_callback([this] {
        flags |= RendererFlags::RESIZE_SWAPCHAIN_BIT;
        return true;
    });
    // screen_rect = { .offset = { 150, 0 }, .extent = { 768, 576 } };
    flags.set(RendererFlags::RESIZE_SCREEN_RECT_BIT | RendererFlags::RESIZE_SWAPCHAIN_BIT);
}

void RendererVulkan::set_screen_rect(ScreenRect rect) {
    if(screen_rect.offset.x != rect.offset_x || screen_rect.offset.y != rect.offset_y ||
       screen_rect.extent.width != rect.width || screen_rect.extent.height != rect.height) {
        screen_rect = { .offset = { rect.offset_x, rect.offset_y }, .extent = { rect.width, rect.height } };
        flags.set(RendererFlags::RESIZE_SCREEN_RECT_BIT);
        // const auto proj = glm::perspective(190.0f, (float)1024.0f / (float)768.0f,
        // 0.0f, 10.0f); Engine::camera()->update_projection(proj);
    }
}

void RendererVulkan::initialize_vulkan() {
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

    const auto* window = Engine::window();

    vks::Win32SurfaceCreateInfoKHR surface_info;
    surface_info.hwnd = glfwGetWin32Window(window->window);
    surface_info.hinstance = GetModuleHandle(nullptr);
    vkCreateWin32SurfaceKHR(vkb_inst.instance, &surface_info, nullptr, &window_surface);

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    auto phys_ret = selector.require_present()
                        .set_surface(window_surface)
                        .set_minimum_version(1, 3)
                        .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)        // for imgui
                        .add_required_extension(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) // for imgui
                        //.add_required_extension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME)
                        .select();
    if(!phys_ret) { throw std::runtime_error{ "Failed to select Vulkan Physical Device. Error: " }; }

    vkb::DeviceBuilder device_builder{ phys_ret.value() };

    vks::PhysicalDeviceSynchronization2Features synch2_features;
    synch2_features.synchronization2 = true;

    vks::PhysicalDeviceDynamicRenderingFeatures dyn_features;
    dyn_features.dynamicRendering = true;

    vks::PhysicalDeviceFeatures2 dev_2_features;
    dev_2_features.features = {
        .geometryShader = true,
        .multiDrawIndirect = true,
        .fragmentStoresAndAtomics = true,
    };

    VkPhysicalDeviceVulkan12Features dev_vk12_features{};
    dev_vk12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    dev_vk12_features.drawIndirectCount = true;
    dev_vk12_features.descriptorBindingVariableDescriptorCount = true;
    dev_vk12_features.descriptorBindingPartiallyBound = true;
    dev_vk12_features.shaderSampledImageArrayNonUniformIndexing = true;
    dev_vk12_features.shaderStorageImageArrayNonUniformIndexing = true;
    dev_vk12_features.descriptorBindingSampledImageUpdateAfterBind = true;
    dev_vk12_features.descriptorBindingStorageImageUpdateAfterBind = true;
    dev_vk12_features.descriptorBindingUniformBufferUpdateAfterBind = true;
    dev_vk12_features.descriptorBindingStorageBufferUpdateAfterBind = true;
    dev_vk12_features.descriptorBindingUpdateUnusedWhilePending = true;
    dev_vk12_features.runtimeDescriptorArray = true;
    dev_vk12_features.bufferDeviceAddress = true;
    dev_vk12_features.hostQueryReset = true;
    dev_vk12_features.scalarBlockLayout = true;
    dev_vk12_features.timelineSemaphore = true;

    vks::PhysicalDeviceAccelerationStructureFeaturesKHR acc_features;
    acc_features.accelerationStructure = true;
    acc_features.descriptorBindingAccelerationStructureUpdateAfterBind = true;

    vks::PhysicalDeviceRayTracingPipelineFeaturesKHR rtpp_features;
    rtpp_features.rayTracingPipeline = true;
    rtpp_features.rayTraversalPrimitiveCulling = true;

    vks::PhysicalDeviceMaintenance5FeaturesKHR maint5_features;
    maint5_features.maintenance5 = true;

    VkPhysicalDeviceRayQueryFeaturesKHR rayq_features{};
    rayq_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayq_features.rayQuery = true;

    auto dev_ret = device_builder.add_pNext(&dev_2_features)
                       .add_pNext(&dyn_features)
                       .add_pNext(&synch2_features)
                       .add_pNext(&dev_vk12_features)
                       .add_pNext(&acc_features)
                       .add_pNext(&rtpp_features)
                       .add_pNext(&rayq_features)
                       //.add_pNext(&maint5_features)
                       .build();
    if(!dev_ret) { throw std::runtime_error{ "Failed to create Vulkan device. Error: " }; }
    vkb::Device vkb_device = dev_ret.value();
    volkLoadDevice(vkb_device.device);

    VkDevice device = vkb_device.device;

    auto graphics_queue_ret = vkb_device.get_queue(vkb::QueueType::graphics);
    if(!graphics_queue_ret) { throw std::runtime_error{ "Failed to get graphics queue. Error: " }; }

    VkQueue graphics_queue = graphics_queue_ret.value();

    VkPhysicalDeviceProperties2 pdev_props{};
    pdev_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    pdev_props.pNext = &rt_props;
    rt_props.pNext = &rt_acc_props;
    vkGetPhysicalDeviceProperties2(phys_ret->physical_device, &pdev_props);

    instance = vkb_inst.instance;
    dev = device;
    pdev = phys_ret->physical_device;
    gqi = vkb_device.get_queue_index(vkb::QueueType::graphics).value();
    gq = vkb_device.get_queue(vkb::QueueType::graphics).value();
    pqi = vkb_device.get_queue_index(vkb::QueueType::present).value();
    pq = vkb_device.get_queue(vkb::QueueType::present).value();
    tqi1 = vkb_device.get_dedicated_queue_index(vkb::QueueType::transfer).has_value()
               ? vkb_device.get_dedicated_queue_index(vkb::QueueType::transfer).value()
               : vkb_device.get_queue_index(vkb::QueueType::transfer).value();
    tq1 = vkb_device.get_dedicated_queue(vkb::QueueType::transfer).has_value()
              ? vkb_device.get_dedicated_queue(vkb::QueueType::transfer).value()
              : vkb_device.get_queue(vkb::QueueType::transfer).value();
    scheduler_gq = QueueScheduler{ gq };

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = inst_ret->fp_vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = inst_ret->fp_vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
    vulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
    vulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    vulkanFunctions.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
    vulkanFunctions.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo allocatorCreateInfo = {};
    allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorCreateInfo.physicalDevice = pdev;
    allocatorCreateInfo.device = dev;
    allocatorCreateInfo.instance = instance;
    allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &vma));
}

// void RendererVulkan::create_swapchain() {
//     if(swapchain) { vkDestroySwapchainKHR(dev, swapchain, nullptr); }
//
//     VkFormat view_formats[]{
//         VK_FORMAT_R8G8B8A8_SRGB,
//         VK_FORMAT_R8G8B8A8_UNORM,
//     };
//
//     VkImageFormatListCreateInfo format_list_info = {
//         .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
//         .viewFormatCount = 2,
//         .pViewFormats = view_formats,
//     };
//
//     vks::SwapchainCreateInfoKHR sinfo{ VkSwapchainCreateInfoKHR{
//         .pNext = &format_list_info,
//         .flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR,
//         .surface = window_surface,
//         .minImageCount = 2,
//         .imageFormat = VK_FORMAT_R8G8B8A8_SRGB,
//         .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
//         .imageExtent = VkExtent2D{ Engine::window()->width, Engine::window()->height },
//         .imageArrayLayers = 1,
//         .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
//         VK_IMAGE_USAGE_SAMPLED_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform =
//         VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .clipped = true,
//     } };
//
//     VK_CHECK(vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain));
//     u32 num_images;
//     vkGetSwapchainImagesKHR(dev, swapchain, &num_images, nullptr);
//
//     std::vector<VkImage> images(num_images);
//     vkGetSwapchainImagesKHR(dev, swapchain, &num_images, images.data());
//
//     swapchain_format = sinfo.imageFormat;
//
//     for(int i = 0; i < 2; ++i) {
//         if(imgui_views[i]) { vkDestroyImageView(dev, imgui_views[i], nullptr); }
//         vkDestroyImageView(dev, std::exchange(swapchain_images[i].view, nullptr), nullptr);
//         swapchain_images[i].image = nullptr;
//
//         swapchain_images[i] = Image{ std::format("swapchain_image_{}", i),
//                                      images.at(i),
//                                      sinfo.imageExtent.width,
//                                      sinfo.imageExtent.height,
//                                      1u,
//                                      1u,
//                                      sinfo.imageArrayLayers,
//                                      sinfo.imageFormat,
//                                      VK_SAMPLE_COUNT_1_BIT,
//                                      sinfo.imageUsage };
//
//         vks::ImageViewCreateInfo imgui_image_view;
//         imgui_image_view.format = VK_FORMAT_R8G8B8A8_UNORM;
//         imgui_image_view.image = images[i];
//         imgui_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
//         imgui_image_view.subresourceRange = {
//             .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
//             .levelCount = 1,
//             .layerCount = 1,
//         };
//         VK_CHECK(vkCreateImageView(dev, &imgui_image_view, nullptr, &imgui_views[i]));
//         set_debug_name(images[i], std::format("swapchain_image_{}", i));
//         set_debug_name(imgui_views[i], std::format("imgui_image_view_{}", i));
//     }
// }

void RendererVulkan::initialize_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Engine::window()->window, true);

    VkFormat color_formats[]{ VK_FORMAT_R8G8B8A8_SRGB };

    Image img{};
    VkSampler samp = samplers.get_sampler();
    PipelineLayout* imgui_layout = &playouts.emplace_back(PipelineLayout{ {
        DescriptorLayout{ .bindings = { DescriptorBinding{ &img, 1, samp } } },
    } });

    DescriptorPool* imgui_dpool = &descpools.emplace_back(DescriptorPool{ imgui_layout, 2 });

    ImGui_ImplVulkan_InitInfo init_info = { 
        .Instance = instance,
        .PhysicalDevice = pdev,
        .Device = dev,
        .QueueFamily = gqi,
        .Queue = gq,
        .DescriptorPool = imgui_dpool->pool,
        .MinImageCount = 2u,
        .ImageCount = 2u,
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = color_formats,
        },
    };
    ImGui_ImplVulkan_Init(&init_info);

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();

    auto cmdimgui = frame_data.get().cmdpool->begin_onetime();
    ImGui_ImplVulkan_CreateFontsTexture();
    frame_data.get().cmdpool->end(cmdimgui);
    scheduler_gq.enqueue_wait_submit({ { cmdimgui } });
    vkQueueWaitIdle(gq);
}

void RendererVulkan::initialize_resources() {
    // staging = std::make_unique<GpuStagingManager>(tq1, tqi1, 1024 * 1024 * 64); // 64MB

    global_buffer = Buffer{ "globals", 512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    vertex_buffer = Buffer{ "vertex_buffer", 0ull,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            false };
    index_buffer = Buffer{ "index_buffer", 0ull,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           false };

    ddgi.radiance_texture = make_image(Image{ "ddgi radiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
    ddgi.irradiance_texture = make_image(Image{ "ddgi irradiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
    ddgi.visibility_texture = make_image(Image{ "ddgi visibility", 1, 1, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                                                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
    ddgi.probe_offsets_texture =
        make_image(Image{ "ddgi probe offsets", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });

    for(u32 i = 0; i < frame_data.data.size(); ++i) {
        auto& fd = frame_data.data[i];
        CommandPool* cmdgq1 = &cmdpools.emplace_back(CommandPool{ gqi });
        fd.sem_swapchain = Semaphore{ dev, false };
        fd.sem_rendering_finished = Semaphore{ dev, false };
        fd.fen_rendering_finished = Fence{ dev, true };
        fd.cmdpool = cmdgq1;
        fd.constants = Buffer{ std::format("constants_{}", i), 64 * 5,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
        mesh_instance_transform_buffers[i] =
            new Buffer{ "mesh instance transforms", 0ull,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    }

    auto samp_ll = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    auto samp_lr = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    DescriptorLayout dl_default{ .bindings = { { { &tlas },
                                                 { ddgi.radiance_texture, 1, VK_IMAGE_LAYOUT_GENERAL, samp_ll },
                                                 { ddgi.radiance_texture },
                                                 { ddgi.irradiance_texture, 1, VK_IMAGE_LAYOUT_GENERAL, samp_ll },
                                                 { ddgi.visibility_texture, 1, VK_IMAGE_LAYOUT_GENERAL, samp_ll },
                                                 { ddgi.probe_offsets_texture },
                                                 { ddgi.irradiance_texture },
                                                 { ddgi.visibility_texture } } },
                                 .variable_binding = 15 };
    dl_default.bindings[15] = { &textures.data_storage(), 1024, samp_lr };

    PipelineLayout* pl_default = &playouts.emplace_back(PipelineLayout{ { dl_default } });
    Pipeline* pp_lit = &pipelines.emplace_back(Pipeline{ { shaders.get_shader("default_unlit/default.vert.glsl"),
                                                           shaders.get_shader("default_unlit/default.frag.glsl") },
                                                         pl_default });
    Pipeline* pp_ddgi_radiance = &pipelines.emplace_back(Pipeline{
        { shaders.get_shader("rtbasic/raygen.rgen.glsl"), shaders.get_shader("rtbasic/miss.rmiss.glsl"),
          shaders.get_shader("rtbasic/shadow.rmiss.glsl"), shaders.get_shader("rtbasic/closest_hit.rchit.glsl"),
          shaders.get_shader("rtbasic/shadow.rchit.glsl") },
        pl_default,
        Pipeline::RaytracingSettings{ .recursion_depth = 2 } });
    Pipeline* pp_ddgi_irradiance =
        &pipelines.emplace_back(Pipeline{ { shaders.get_shader("rtbasic/probe_irradiance.comp.glsl") }, pl_default });
    Pipeline* pp_ddgi_offsets =
        &pipelines.emplace_back(Pipeline{ { shaders.get_shader("rtbasic/probe_offset.comp.glsl") }, pl_default });

    for(u32 i = 0; i < frame_data.data.size(); ++i) {
        auto& fd = frame_data.data[i];
        DescriptorPool* dp = &descpools.emplace_back(DescriptorPool{ pl_default, 4 });
        fd.descpool = dp;
        fd.passes = RenderPasses{ .ddgi_radiance = { pp_ddgi_radiance, dp },
                                  .ddgi_irradiance = { pp_ddgi_irradiance, dp },
                                  .ddgi_offsets = { pp_ddgi_offsets, dp },
                                  .default_lit = { pp_lit, dp } };
        fd.depth_buffer =
            make_image(Image{ std::format("depth_buffer_{}", i), Engine::window()->width, Engine::window()->height, 1, 1, 1,
                              VK_FORMAT_D16_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT });
    }
}

void RendererVulkan::update() {
    if(Engine::window()->width == 0 || Engine::window()->height == 0) { return; }
    if(flags.test_clear(RendererFlags::DIRTY_GEOMETRY_BATCHES_BIT)) { upload_staged_models(); }
    if(flags.test_clear(RendererFlags::DIRTY_MESH_INSTANCES)) {
        upload_instances();
        upload_transforms();
    }
    if(flags.test_clear(RendererFlags::DIRTY_MESH_BLAS_BIT)) { build_blas(); }
    if(flags.test_clear(RendererFlags::DIRTY_TLAS_BIT)) {
        build_tlas();
        // TODO: prepare ddgi on scene update
        if(Engine::frame_num() < 100) { update_ddgi(); }
    }
    if(flags.test_clear(RendererFlags::REFIT_TLAS_BIT)) { refit_tlas(); }
    // if(flags.test_clear(RendererFlags::UPLOAD_MESH_INSTANCE_TRANSFORMS_BIT)) { upload_transforms(); }
    if(flags.test_clear(RendererFlags::RESIZE_SWAPCHAIN_BIT)) {
        vkQueueWaitIdle(gq);
        swapchain.create();
        // create_swapchain();
    }
    if(flags.test_clear(RendererFlags::RESIZE_SCREEN_RECT_BIT)) {
        vkQueueWaitIdle(gq);
        for(int i = 0; i < frame_data.data.size(); ++i) {
            /* output_images[i] =
                 Image{ std::format("render_output_image{}", i),
                        screen_rect.extent.width,
                        screen_rect.extent.height,
                        1u,
                        1u,
                        1u,
                        VK_FORMAT_R8G8B8A8_SRGB,
                        VK_SAMPLE_COUNT_1_BIT,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };*/

            *frame_data.data[i].depth_buffer = Image{
                std::format("depth_buffer_{}", i), Engine::window()->width, Engine::window()->height, 1, 1, 1, VK_FORMAT_D16_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            };
        }
    }

    auto& fd = frame_data.get();
    const auto frame_num = Engine::frame_num();
    vkWaitForFences(dev, 1, &fd.fen_rendering_finished.fence, true, 16'000'000);
    fd.cmdpool->reset();
    fd.descpool->reset();

    u32 swapchain_index{};
    Image* swapchain_image{};
    {
        VkResult acquire_ret;
        swapchain_index = swapchain.acquire(&acquire_ret, 16'000'000, fd.sem_swapchain.semaphore);
        if(acquire_ret != VK_SUCCESS) {
            ENG_WARN("Acquire image failed with: {}", static_cast<u32>(acquire_ret));
            return;
        }
        swapchain_image = &swapchain.images[swapchain_index];
    }

    if(Engine::frame_num() > 0) {
        // for(u32 i = 0; i < ddgi.debug_probes.size(); ++i) {
        //     auto h = Handle<Entity>{ *ddgi.debug_probes.at(i) };
        //     auto& t = Engine::ec()->get<cmps::Transform>(h);
        //     const auto tv =
        //         ddgi.probe_start +
        //         ddgi.probe_walk * glm::vec3{ i % ddgi.probe_counts.x, (i / ddgi.probe_counts.x) % ddgi.probe_counts.y,
        //                                      i / (ddgi.probe_counts.x * ddgi.probe_counts.y) } +
        //         ((glm::vec3*)ddgi.debug_probe_offsets_buffer.mapped)[i];
        //     t.transform = glm::translate(glm::mat4{ 1.0f }, tv) * glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.2f });
        //     Engine::scene()->update_transform(h);
        // }
    }

    vkResetFences(dev, 1, &frame_data.get().fen_rendering_finished.fence);

    /*if(!primitives.desc_pool) { primitives.desc_pool = descriptor_pool_allocator.allocate_pool(layouts.at(0), 0, 2); }
    descriptor_pool_allocator.reset_pool(primitives.desc_pool);*/

    // VkDescriptorSet frame_desc_set = fd.passes.default_lit.sets[0];
    // static VkSampler linear_sampler = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // DescriptorSetWriter per_frame_set_writer;
    // per_frame_set_writer.write(0, 0, tlas)
    //     .write(1, 0, rt_image.view, {}, VK_IMAGE_LAYOUT_GENERAL)
    //     .write(2, 0, ddgi.radiance_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
    //     .write(3, 0, ddgi.irradiance_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
    //     .write(4, 0, ddgi.visibility_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
    //     .write(5, 0, ddgi.probe_offsets_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL)
    //     .write(6, 0, ddgi.irradiance_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL)
    //     .write(7, 0, ddgi.visibility_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL);
    // for(u32 i = 0; i < textures.size(); ++i) {
    //     per_frame_set_writer.write(15, i, textures.at(i).view, linear_sampler, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    // }
    // per_frame_set_writer.update(frame_desc_set, layouts.at(0), 0);

    const float hx = (halton(Engine::frame_num() % 4u, 2) * 2.0 - 1.0);
    const float hy = (halton(Engine::frame_num() % 4u, 3) * 2.0 - 1.0);
    const glm::mat3 rand_mat =
        glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

    {
        float globals[16 * 4 + 12];
        const auto view = Engine::camera()->get_view();
        const auto proj = glm::perspective(glm::radians(45.0f), 1024.0f / 768.0f, 0.01f, 20.0f); // Engine::camera()->get_projection();
        const auto inv_view = glm::inverse(view);
        const auto inv_proj = glm::inverse(proj);
        memcpy(&globals[0], &view, sizeof(glm::mat4));
        memcpy(&globals[16], &proj, sizeof(glm::mat4));
        memcpy(&globals[32], &inv_view, sizeof(glm::mat4));
        memcpy(&globals[48], &inv_proj, sizeof(glm::mat4));
        memcpy(&globals[64], &rand_mat, sizeof(glm::mat3));
        global_buffer.push_data(globals, sizeof(globals), 0ull);
    }

    // ImageStatefulBarrier output_image_barrier{ *swapchain_image };
    ImageStatefulBarrier swapchain_image_barrier{ *swapchain_image };
    ImageStatefulBarrier depth_buffer_barrier{ *fd.depth_buffer };
    ImageStatefulBarrier radiance_image_barrier{ *ddgi.radiance_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                 VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_WRITE_BIT };
    ImageStatefulBarrier irradiance_image_barrier{ *ddgi.irradiance_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT };
    ImageStatefulBarrier visibility_image_barrier{ *ddgi.visibility_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT };
    ImageStatefulBarrier offset_image_barrier{ *ddgi.probe_offsets_texture, VK_IMAGE_LAYOUT_GENERAL,
                                               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT };

    auto cmd = fd.cmdpool->begin_onetime();

    if(flags.test_clear(RendererFlags::DIRTY_TRANSFORMS_BIT)) {
        for(const auto& e : update_positions) {
            const auto idx = mesh_instance_idxs.at(e);
            const auto& mi = mesh_instances.at(idx);
            const auto offset = idx * sizeof(glm::mat4);
            const auto& t = Engine::scene()->get_final_transform(mi.entity);
            vkCmdUpdateBuffer(cmd, mesh_instance_transform_buffers[0]->buffer, offset, sizeof(glm::mat4), &t);
            if(Engine::ec()->get<cmps::RenderMesh>(mi.entity).blas_handle) {
                flags.set(RendererFlags::DIRTY_TLAS_BIT); // TODO: SHould be refit tlas
            }
        }
        update_positions.clear();
    }

    swapchain_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    depth_buffer_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    if(tlas && ddgi.buffer.buffer) {
        ddgi.buffer.push_data(&frame_num, sizeof(u32), offsetof(DDGI::GPULayout, frame_num));

        /*output_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
           VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);*/

        const u32 handle_size_aligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);

        vks::StridedDeviceAddressRegionKHR raygen_sbt;
        raygen_sbt.deviceAddress = fd.passes.ddgi_radiance.pipeline->raytracing_settings.sbt->bda;
        raygen_sbt.size = handle_size_aligned * 1;
        raygen_sbt.stride = handle_size_aligned;

        vks::StridedDeviceAddressRegionKHR miss_sbt;
        miss_sbt.deviceAddress = fd.passes.ddgi_radiance.pipeline->raytracing_settings.sbt->bda;
        miss_sbt.size = handle_size_aligned * 2;
        miss_sbt.stride = handle_size_aligned;

        vks::StridedDeviceAddressRegionKHR hit_sbt;
        hit_sbt.deviceAddress = fd.passes.ddgi_radiance.pipeline->raytracing_settings.sbt->bda;
        hit_sbt.size = handle_size_aligned * 2;
        hit_sbt.stride = handle_size_aligned;

        vks::StridedDeviceAddressRegionKHR callable_sbt;

        const auto* window = Engine::window();
        u32 mode = 0;
        // clang-format off
        fd.passes.ddgi_radiance.push_constant(cmd, 0 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &global_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 1 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instances_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 2 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &vertex_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &index_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 4 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi.buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 5 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &tlas_mesh_offsets_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 6 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &blas_mesh_offsets_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 7 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &triangle_geo_inst_id_buffer.bda);
        fd.passes.ddgi_radiance.push_constant(cmd, 8 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instance_transform_buffers[0]->bda);
        // clang-format on

        fd.passes.ddgi_radiance.update_desc_sets();
        fd.passes.ddgi_radiance.bind(cmd);
        fd.passes.ddgi_radiance.bind_desc_sets(cmd);

        /*vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                          pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, 0, 1, &frame_desc_set, 0, nullptr);*/

        // radiance pass
        mode = 1;
        fd.passes.ddgi_radiance.push_constant(cmd, 9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        /*vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);*/
        vkCmdTraceRaysKHR(cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, ddgi.rays_per_probe,
                          ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z, 1);

        radiance_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.at(RenderPipelineType::DDGI_PROBE_UPDATE).pipeline);
        /*vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, 0, 1, &frame_desc_set, 0, nullptr);*/
        fd.passes.ddgi_irradiance.update_desc_sets();
        fd.passes.ddgi_irradiance.bind(cmd);
        fd.passes.ddgi_irradiance.bind_desc_sets(cmd);

        // irradiance pass, only need radiance texture
        mode = 0;
        /*vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);*/
        fd.passes.ddgi_irradiance.push_constant(cmd, 9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.irradiance_texture->width / 8u),
                      std::ceilf((float)ddgi.irradiance_texture->height / 8u), 1u);

        // visibility pass, only need radiance texture
        mode = 1;
        fd.passes.ddgi_irradiance.push_constant(cmd, 9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        /*vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);*/
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.visibility_texture->width / 8u),
                      std::ceilf((float)ddgi.visibility_texture->height / 8u), 1u);

        // probe offset pass, only need radiance texture to complete
        fd.passes.ddgi_offsets.bind(cmd);
        /*vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.at(RenderPipelineType::DDGI_PROBE_OFFSET).pipeline);*/
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.probe_offsets_texture->width / 8.0f),
                      std::ceilf((float)ddgi.probe_offsets_texture->height / 8.0f), 1u);

        irradiance_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        visibility_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        offset_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    }

    // default pass
    {
        VkRenderingAttachmentInfo r_col_att_1{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_image->view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
        };
        VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };

        VkRenderingAttachmentInfo r_dep_att{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = fd.depth_buffer->view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .depthStencil = { 1.0f, 0 } },
        };

        VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .extent = { .width = Engine::window()->width, .height = Engine::window()->height } },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = r_col_atts,
            .pDepthAttachment = &r_dep_att,
        };

        VkDeviceSize vb_offsets[]{ 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, vb_offsets);
        vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        fd.passes.default_lit.update_desc_sets();
        fd.passes.default_lit.bind(cmd);
        fd.passes.default_lit.bind_desc_sets(cmd);
        /*vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.at(RenderPipelineType::DEFAULT_UNLIT).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelines.at(RenderPipelineType::DEFAULT_UNLIT).layout, 0, 1, &frame_desc_set, 0, nullptr);*/

        vkCmdBeginRendering(cmd, &rendering_info);
        VkRect2D r_sciss_1{ .offset = {}, .extent = { Engine::window()->width, Engine::window()->height } };
        VkViewport r_view_1{ .x = 0.0f,
                             .y = static_cast<float>(Engine::window()->height),
                             .width = static_cast<float>(Engine::window()->width),
                             .height = -static_cast<float>(Engine::window()->height),
                             .minDepth = 0.0f,
                             .maxDepth = 1.0f };
        vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
        vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
        // clang-format off
        fd.passes.default_lit.push_constant(cmd, 0 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &global_buffer.bda);
        fd.passes.default_lit.push_constant(cmd, 1 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instances_buffer.bda);
        fd.passes.default_lit.push_constant(cmd, 2 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instance_transform_buffers[0]->bda);
        fd.passes.default_lit.push_constant(cmd, 3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi.buffer.bda);
        // clang-format on
        vkCmdDrawIndexedIndirectCount(cmd, indirect_draw_buffer.buffer, sizeof(IndirectDrawCommandBufferHeader),
                                      indirect_draw_buffer.buffer, 0ull, max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
        vkCmdEndRendering(cmd);

        /*output_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);*/

        // TODO: IMPLEMENT IMGUI
        // assert(false);
        /*ImDrawData* im_draw_data = ImGui::GetDrawData();
        if(im_draw_data) {
            VkRenderingAttachmentInfo i_col_atts[]{
                VkRenderingAttachmentInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = swapchain_images[sw_img_idx].view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
                },
            };
            VkRenderingInfo imgui_rendering_info{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = VkRect2D{ .offset = { 0, 0 }, .extent = { Engine::window()->width,
        Engine::window()->height } }, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = i_col_atts,
            };
            vkCmdBeginRendering(cmd, &imgui_rendering_info);
            vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
            vkCmdSetViewport(cmd, 0, 1, &r_view_1);
            ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
            vkCmdEndRendering(cmd);
        }*/
    }

    swapchain_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_NONE, VK_ACCESS_NONE);
    fd.cmdpool->end(cmd);

    // TODO: Implement waiting for timeline semaphore and presenting
    std::pair<Semaphore*, u32> waits[]{ { &fd.sem_swapchain, 0 } };
    VkPipelineStageFlags2 wait_stages[]{ VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
    std::pair<Semaphore*, u32> signals[]{ { &fd.sem_rendering_finished, 0 } };
    VkPipelineStageFlags2 signal_stages[]{ VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
    QueueSubmit gqs{};
    QueueSubmision gqsubs{ .cmds = { &cmd, 1 },
                           .wait_sems = { waits, 1 },
                           .signal_sems = { signals, 1 },
                           .wait_stages = { wait_stages, 1 },
                           .signal_stages = { signal_stages, 1 } };
    gqs.submit(gq, { &gqsubs, 1 }, &fd.fen_rendering_finished);

    vks::PresentInfoKHR pinfo;
    pinfo.swapchainCount = 1;
    pinfo.pSwapchains = &swapchain.swapchain;
    pinfo.pImageIndices = &swapchain_index;
    pinfo.waitSemaphoreCount = 1;
    pinfo.pWaitSemaphores = &fd.sem_rendering_finished.semaphore;
    vkQueuePresentKHR(gq, &pinfo);

    /*scheduler_gq.enqueue_wait_submit(
        RecordingSubmitInfo{
            .buffers = { cmd },
            .waits = { { primitives.sem_swapchain_image, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } },
            .signals = { { primitives.sem_rendering_finished, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } },
        },
        primitives.fen_rendering_finished);

    vks::PresentInfoKHR pinfo;
    pinfo.swapchainCount = 1;
    pinfo.pSwapchains = &swapchain;
    pinfo.pImageIndices = &sw_img_idx;
    pinfo.waitSemaphoreCount = 1;
    pinfo.pWaitSemaphores = &primitives.sem_rendering_finished;
    vkQueuePresentKHR(gq, &pinfo);*/
}

Handle<RenderTexture> RendererVulkan::batch_texture(const RenderTexture& batch) {
    Handle<Image> handle =
        textures.insert(Image{ batch.name, batch.width, batch.height, batch.depth, batch.mips, 1u, VK_FORMAT_R8G8B8A8_SRGB,
                               VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
    upload_images.push_back(UploadImage{ handle, { batch.data.begin(), batch.data.end() } });
    return Handle<RenderTexture>{ *handle };
}

Handle<MaterialBatch> RendererVulkan::batch_material(const MaterialBatch& batch) {
    return Handle<MaterialBatch>{ *materials.insert(RenderMaterial{ .color_texture = batch.color_texture }) };
}

Handle<RenderGeometry> RendererVulkan::batch_geometry(const GeometryDescriptor& batch) {
    const auto total_vertices = get_total_vertices();
    const auto total_indices = get_total_indices();

    RenderGeometry geometry{ .metadata = geometry_metadatas.emplace(),
                             .vertex_offset = total_vertices,
                             .vertex_count = (u32)batch.vertices.size(),
                             .index_offset = total_indices,
                             .index_count = (u32)batch.indices.size() };

    upload_vertices.insert(upload_vertices.end(), batch.vertices.begin(), batch.vertices.end());
    upload_indices.insert(upload_indices.end(), batch.indices.begin(), batch.indices.end());

    Handle<RenderGeometry> handle = geometries.insert(geometry);

    flags.set(RendererFlags::DIRTY_GEOMETRY_BATCHES_BIT);

    // clang-format off
    ENG_LOG("Batching geometry: [VXS: {:.2f} KB, IXS: {:.2f} KB]", 
            static_cast<float>(batch.vertices.size_bytes()) / 1000.0f,
            static_cast<float>(batch.indices.size_bytes()) / 1000.0f);
    // clang-format on

    return handle;
}

Handle<RenderMesh> RendererVulkan::batch_mesh(const MeshDescriptor& batch) {
    RenderMesh mesh_batch{ .geometry = batch.geometry,
                           .metadata = mesh_metadatas.emplace(),
                           .vertex_offset = batch.vertex_offset,
                           .vertex_count = batch.vertex_count,
                           .index_offset = batch.index_offset,
                           .index_count = batch.index_count };
    return meshes.insert(mesh_batch);
}

Handle<MeshInstance> RendererVulkan::instance_mesh(const InstanceSettings& settings) {
    assert(settings.entity);

    const auto handle = Handle<MeshInstance>{ generate_handle };
    mesh_instances.push_back(MeshInstance{
        .handle = handle,
        .entity = settings.entity,
        .mesh = settings.mesh,
        .material = settings.material,
    });

    flags.set(RendererFlags::DIRTY_MESH_INSTANCES);
    if(settings.flags.test(InstanceFlags::RAY_TRACED_BIT)) { flags.set(RendererFlags::DIRTY_TLAS_BIT); }

    return handle;
}

Handle<BLASInstance> RendererVulkan::instance_blas(const BLASInstanceSettings& settings) {
    Handle<BLASInstance> handle{ generate_handle };
    auto it = std::find_if(mesh_instances.begin(), mesh_instances.end(),
                           [&settings](const MeshInstance& e) { return e.handle == settings.render_instance; });
    blas_instances.push_back(BLASInstance{ .handle = handle, .render_handle = settings.render_instance, .mesh_batch = it->mesh });
    flags.set(RendererFlags::DIRTY_TLAS_BIT);
    if(!meshes.at(it->mesh).metadata->blas) {
        meshes.at(it->mesh).flags.set(MeshBatchFlags::DIRTY_BLAS_BIT);
        flags.set(RendererFlags::DIRTY_MESH_BLAS_BIT);
    }
    return handle;
}

void RendererVulkan::update_transform(Handle<MeshInstance> handle) {
    update_positions.push_back(handle);
    flags.set(RendererFlags::DIRTY_TRANSFORMS_BIT);
}

void RendererVulkan::upload_model_textures() {
    std::vector<Buffer> bs;
    bs.reserve(upload_images.size());
    auto cmd = frame_data.get().cmdpool->begin_onetime();
    for(auto& tex : upload_images) {
        auto& b = bs.emplace_back(Buffer{ "staging buffer", tex.rgba_data.size(),
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true });
        b.push_data(tex.rgba_data, 0);
        Image& img = textures.at(tex.image_handle);
        img.transition_layout(cmd, VK_PIPELINE_STAGE_NONE, VK_ACCESS_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
            .imageOffset = {},
            .imageExtent = { img.width, img.height, 1 },
        };
        vkCmdCopyBufferToImage(cmd, b.buffer, img.image, img.current_layout, 1, &copy);
        img.transition_layout(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }
    frame_data.get().cmdpool->end(cmd);
    vks::CommandBufferSubmitInfo cmdinfo;
    cmdinfo.commandBuffer = cmd;
    vks::SubmitInfo2 info;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = &cmdinfo;
    Fence f{ dev, false };
    VK_CHECK(vkQueueSubmit2(gq, 1, &info, f.fence));
    vkWaitForFences(dev, 1, &f.fence, true, ~0u);
    for(auto& b : bs) {
        b.deallocate();
    }
    upload_images.clear();

    // VkSemaphore acquire_sem = create_semaphore();
    // VkSemaphore release_sem = create_semaphore();
    // VkSemaphore transfer_done_sem = create_semaphore();
    // VkCommandBuffer acquire_cmd = get_primitives().cmdpool.begin_onetime();
    // VkCommandBuffer release_cmd = get_primitives().cmdpool.begin_onetime();
    // VkCommandBuffer cmd = get_primitives().cmdpool.begin_onetime();
    // std::vector<GpuStagingUpload> uploads;
    // uploads.reserve(upload_images.size());

    // for(auto& tex : upload_images) {
    //     Image* img = &textures.at(tex.image_handle);
    //     {
    //         VkImageMemoryBarrier img_barrier{
    //             .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    //             .srcAccessMask = VK_ACCESS_NONE,
    //             .dstAccessMask = VK_ACCESS_NONE,
    //             .oldLayout = img->current_layout,
    //             .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //             .srcQueueFamilyIndex = gqi,
    //             .dstQueueFamilyIndex = tqi1,
    //             .image = img->image,
    //             .subresourceRange = { .aspectMask = img->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
    //         };
    //         vkCmdPipelineBarrier(release_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0,
    //                              {}, 1, &img_barrier);
    //     }
    //     {
    //         VkImageMemoryBarrier img_barrier{
    //             .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    //             .srcAccessMask = VK_ACCESS_NONE,
    //             .dstAccessMask = VK_ACCESS_NONE,
    //             .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //             .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //             .srcQueueFamilyIndex = tqi1,
    //             .dstQueueFamilyIndex = gqi,
    //             .image = img->image,
    //             .subresourceRange = { .aspectMask = img->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
    //         };
    //         vkCmdPipelineBarrier(acquire_cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, 0, {}, 0,
    //                              {}, 1, &img_barrier);
    //     }
    //     {
    //         VkImageMemoryBarrier img_barrier{
    //             .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    //             .srcAccessMask = VK_ACCESS_NONE,
    //             .dstAccessMask = VK_ACCESS_NONE,
    //             .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //             .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
    //             .srcQueueFamilyIndex = gqi,
    //             .dstQueueFamilyIndex = gqi,
    //             .image = img->image,
    //             .subresourceRange = { .aspectMask = img->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
    //         };
    //         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, 0, {}, 0, {}, 1, &img_barrier);
    //     }
    //     uploads.push_back(GpuStagingUpload{ .src_queue_idx = gqi,
    //                                         .dst = img,
    //                                         .src = std::span{ tex.rgba_data.begin(), tex.rgba_data.end() },
    //                                         .dst_offset = VkOffset3D{},
    //                                         .dst_img_rel_sem = release_sem,
    //                                         .size_bytes = tex.rgba_data.size() });
    // }
    // get_primitives().cmdpool.end(release_cmd);
    // get_primitives().cmdpool.end(acquire_cmd);
    // get_primitives().cmdpool.end(cmd);

    // scheduler_gq.enqueue_wait_submit({ .buffers = { release_cmd },
    //                                    .signals = { { release_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });

    // std::atomic_flag flag;
    // staging->send_to(std::span{ uploads.data(), uploads.size() }, release_sem, transfer_done_sem, &flag);
    // flag.wait(false);

    // vks::FenceCreateInfo fence_info{};
    // VkFence fence;
    // VK_CHECK(vkCreateFence(dev, &fence_info, {}, &fence));
    // scheduler_gq.enqueue_wait_submit({ .buffers = { acquire_cmd },
    //                                    .waits = { { transfer_done_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    //                                    .signals = { { acquire_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });
    // scheduler_gq.enqueue_wait_submit({ .buffers = { cmd }, .waits = { { acquire_sem,
    // VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } }, fence); VK_CHECK(vkWaitForFences(dev, 1, &fence, true, 1'000'000));
    // vkDestroyFence(dev, fence, {}); destroy_semaphore(release_sem); destroy_semaphore(acquire_sem);
    // destroy_semaphore(transfer_done_sem); upload_images.clear();
}

void RendererVulkan::upload_staged_models() {
    upload_model_textures();
    vertex_buffer.push_data(upload_vertices);
    index_buffer.push_data(upload_indices);
    upload_vertices.clear();
    upload_indices.clear();
}

void RendererVulkan::upload_instances() {
    std::sort(mesh_instances.begin(), mesh_instances.end(), [](const MeshInstance& a, const MeshInstance& b) {
        if(a.material >= b.material) { return false; }
        if(a.mesh >= b.mesh) { return false; }
        return true;
    });

    mesh_instance_idxs.clear();
    mesh_instance_idxs.reserve(mesh_instances.size());
    for(u32 i = 0; i < mesh_instances.size(); ++i) {
        mesh_instance_idxs[mesh_instances.at(i).handle] = i;
    }

    const auto total_triangles = get_total_triangles();
    std::vector<GPUMeshInstance> gpu_mesh_instances;
    std::vector<VkDrawIndexedIndirectCommand> gpu_draw_commands;
    IndirectDrawCommandBufferHeader gpu_draw_header;

    for(u32 i = 0u; i < mesh_instances.size(); ++i) {
        const MeshInstance& mi = mesh_instances.at(i);
        const RenderMesh& mb = meshes.at(mi.mesh);
        const RenderGeometry& geom = geometries.at(mb.geometry);
        const RenderMaterial& mat = materials.at(Handle<RenderMaterial>{ *mi.material });
        gpu_mesh_instances.push_back(GPUMeshInstance{ .vertex_offset = geom.vertex_offset + mb.vertex_offset,
                                                      .index_offset = geom.index_offset + mb.index_offset,
                                                      .color_texture_idx =
                                                          (u32)textures.find_idx(Handle<Image>{ *mat.color_texture }) });
        if(i == 0 || mesh_instances.at(i - 1).mesh != mi.mesh) {
            gpu_draw_commands.push_back(VkDrawIndexedIndirectCommand{ .indexCount = mb.index_count,
                                                                      .instanceCount = 1,
                                                                      .firstIndex = geom.index_offset + mb.index_offset,
                                                                      .vertexOffset = (s32)(geom.vertex_offset + mb.vertex_offset),
                                                                      .firstInstance = i });
        } else {
            ++gpu_draw_commands.back().instanceCount;
        }
    }

    gpu_draw_header.draw_count = gpu_draw_commands.size();
    gpu_draw_header.geometry_instance_count = mesh_instances.size();
    max_draw_count = gpu_draw_commands.size();

    // clang-format off
    indirect_draw_buffer = Buffer{"indirect draw", sizeof(IndirectDrawCommandBufferHeader) + gpu_draw_commands.size() * sizeof(gpu_draw_commands[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false};
    indirect_draw_buffer.push_data(&gpu_draw_header, sizeof(IndirectDrawCommandBufferHeader));
    indirect_draw_buffer.push_data(gpu_draw_commands);

    mesh_instances_buffer = Buffer{"mesh instances", gpu_mesh_instances.size() * sizeof(gpu_mesh_instances[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    mesh_instances_buffer.push_data(gpu_mesh_instances);
    // clang-format on
}

void RendererVulkan::upload_transforms() {
    Buffer* dst_transforms = mesh_instance_transform_buffers[1];

    std::vector<glm::mat4> transforms;
    transforms.reserve(mesh_instances.size());
    std::transform(mesh_instances.begin(), mesh_instances.end(), std::back_inserter(transforms),
                   [](const MeshInstance& e) { return Engine::scene()->get_final_transform(e.entity); });

    dst_transforms->push_data(transforms, 0ull);
    std::swap(mesh_instance_transform_buffers[0], mesh_instance_transform_buffers[1]);
}

void RendererVulkan::build_pipelines() {
    // This function should not be called
    assert(false);

    /* RenderPipelineLayout default_layout = RendererPipelineLayoutBuilder{}
                                               .add_set_binding(0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                                               .add_set_binding(0, 1, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                               .add_set_binding(0, 2, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                               .add_set_binding(0, 3, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                                               .add_set_binding(0, 4, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                                               .add_set_binding(0, 5, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                               .add_set_binding(0, 6, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                               .add_set_binding(0, 7, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                                               .add_set_binding(0, 15, 1024, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                                               .add_variable_descriptor_count(0)
                                               .set_push_constants(128)
                                               .build();
     RenderPipelineLayout imgui_layout =
         RendererPipelineLayoutBuilder{}
             .add_set_binding(0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, VK_SHADER_STAGE_FRAGMENT_BIT)
             .set_push_constants(16, VK_SHADER_STAGE_VERTEX_BIT)
             .build();

     pipelines[RenderPipelineType::DEFAULT_UNLIT] = RenderPipelineWrapper{
         .pipeline =
             RendererGraphicsPipelineBuilder{}
                 .set_vertex_binding(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
                 .set_vertex_input(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos))
                 .set_vertex_input(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, nor))
                 .set_vertex_input(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv))
                 .add_color_attachment_format(VK_FORMAT_R8G8B8A8_SRGB)
                 .set_depth_attachment_format(VK_FORMAT_D32_SFLOAT)
                 .set_stage(VK_SHADER_STAGE_VERTEX_BIT, shader_modules.at(ShaderModuleType::DEFAULT_UNLIT_VERTEX).module)
                 .set_stage(VK_SHADER_STAGE_FRAGMENT_BIT, shader_modules.at(ShaderModuleType::DEFAULT_UNLIT_FRAGMENT).module)
                 .set_depth_test(true, VK_COMPARE_OP_LESS)
                 .set_layout(default_layout.layout)
                 .set_dynamic_state(VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT)
                 .set_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT)
                 .build(),
         .layout = default_layout.layout,
     };

     pipelines[RenderPipelineType::DDGI_PROBE_RAYCAST] = RenderPipelineWrapper{
         .pipeline = RendererRaytracingPipelineBuilder{}
                         .set_layout(default_layout.layout)
                         .add_raygen_stage(shader_modules.at(ShaderModuleType::RT_BASIC_RAYGEN).module)
                         .add_miss_stage(shader_modules.at(ShaderModuleType::RT_BASIC_MISS).module)
                         .add_miss_stage(shader_modules.at(ShaderModuleType::RT_BASIC_SHADOW_MISS).module)
                         .add_closest_hit_stage(shader_modules.at(ShaderModuleType::RT_BASIC_CLOSEST_HIT).module)
                         .add_closest_hit_stage(shader_modules.at(ShaderModuleType::RT_BASIC_SHADOW_HIT).module)
                         .set_recursion_depth(2)
                         .build(),
         .layout = default_layout.layout,
         .rt_shader_group_count = 5
     };

     pipelines[RenderPipelineType::DDGI_PROBE_UPDATE] = RenderPipelineWrapper{
         .pipeline = RendererComputePipelineBuilder{}
                         .set_layout(default_layout.layout)
                         .set_stage(shader_modules.at(ShaderModuleType::RT_BASIC_PROBE_IRRADIANCE_COMPUTE).module)
                         .build(),
         .layout = default_layout.layout,
     };

     pipelines[RenderPipelineType::DDGI_PROBE_OFFSET] = RenderPipelineWrapper{
         .pipeline = RendererComputePipelineBuilder{}
                         .set_layout(default_layout.layout)
                         .set_stage(shader_modules.at(ShaderModuleType::RT_BASIC_PROBE_PROBE_OFFSET_COMPUTE).module)
                         .build(),
         .layout = default_layout.layout,
     };

     layouts.push_back(default_layout);
     layouts.push_back(imgui_layout);

     imgui_desc_pool =
         descriptor_pool_allocator.allocate_pool(imgui_layout, 0, 16, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);*/
}

void RendererVulkan::build_sbt() {
    assert(false);
    // const RenderPipelineWrapper& pipeline = pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST);
    // const u32 handleSize = rt_props.shaderGroupHandleSize;
    // const u32 handleSizeAligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);
    // const u32 groupCount = static_cast<u32>(pipeline.rt_shader_group_count);
    // const u32 sbtSize = groupCount * handleSizeAligned;

    // std::vector<uint8_t> shaderHandleStorage(sbtSize);
    // vkGetRayTracingShaderGroupHandlesKHR(dev, pipeline.pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

    // const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
    // VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT; sbt = Buffer{ "buffer_sbt", sbtSize, bufferUsageFlags, false };
    // sbt.push_data(shaderHandleStorage);
}

void RendererVulkan::create_rt_output_image() {
    assert(false);
    /*   const auto* window = Engine::window();
       rt_image = Image{
           "rt_image", window->width, window->height, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
       };

       auto cmd = get_primitives().cmdpool.begin_onetime();
       rt_image.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                  VK_ACCESS_2_SHADER_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
       get_primitives().cmdpool.end(cmd);
       scheduler_gq.enqueue_wait_submit({ { cmd } });
       vkQueueWaitIdle(gq);*/
}

void RendererVulkan::build_blas() {
    vks::AccelerationStructureGeometryTrianglesDataKHR triangles;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex_buffer.bda;
    triangles.vertexStride = sizeof(Vertex);
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = index_buffer.bda;
    triangles.maxVertex = get_total_vertices() - 1u;

    std::vector<const RenderMesh*> dirty_batches;
    std::vector<vks::AccelerationStructureGeometryKHR> blas_geos;
    std::vector<vks::AccelerationStructureBuildGeometryInfoKHR> blas_geo_build_infos;
    std::vector<u32> scratch_sizes;
    std::vector<vks::AccelerationStructureBuildRangeInfoKHR> ranges;
    Buffer scratch_buffer;

    blas_geos.reserve(meshes.size());

    for(auto& mb : meshes) {
        if(!mb.flags.test_clear(MeshBatchFlags::DIRTY_BLAS_BIT)) { continue; }
        RenderGeometry& geom = geometries.at(mb.geometry);
        MeshMetadata& meta = mesh_metadatas.at(mb.metadata);
        dirty_batches.push_back(&mb);

        vks::AccelerationStructureGeometryKHR& blas_geo = blas_geos.emplace_back();
        blas_geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        blas_geo.geometry.triangles = triangles;
        blas_geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        vks::AccelerationStructureBuildGeometryInfoKHR& build_geometry = blas_geo_build_infos.emplace_back();
        build_geometry.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_geometry.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_geometry.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_geometry.geometryCount = 1;
        build_geometry.pGeometries = &blas_geo;

        const u32 primitive_count = mb.index_count / 3u;
        vks::AccelerationStructureBuildSizesInfoKHR build_size_info;
        vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_geometry,
                                                &primitive_count, &build_size_info);

        meta.blas_buffer =
            Buffer{ "blas_buffer", build_size_info.accelerationStructureSize,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
        scratch_sizes.push_back(align_up(build_size_info.buildScratchSize,
                                         static_cast<VkDeviceSize>(rt_acc_props.minAccelerationStructureScratchOffsetAlignment)));

        vks::AccelerationStructureCreateInfoKHR blas_info;
        blas_info.buffer = meta.blas_buffer.buffer;
        blas_info.size = build_size_info.accelerationStructureSize;
        blas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VK_CHECK(vkCreateAccelerationStructureKHR(dev, &blas_info, nullptr, &meta.blas));
    }

    const auto total_scratch_size = std::accumulate(scratch_sizes.begin(), scratch_sizes.end(), 0ul);
    scratch_buffer = Buffer{ "blas_scratch_buffer", total_scratch_size, rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    for(u32 i = 0, scratch_offset = 0; const auto& acc_geoms : blas_geos) {
        const RenderMesh& mb = *dirty_batches.at(i);
        const RenderGeometry& geom = geometries.at(mb.geometry);
        const MeshMetadata& meta = mesh_metadatas.at(mb.metadata);
        blas_geo_build_infos.at(i).scratchData.deviceAddress = scratch_buffer.bda + scratch_offset;
        blas_geo_build_infos.at(i).dstAccelerationStructure = meta.blas;

        vks::AccelerationStructureBuildRangeInfoKHR& range_info = ranges.emplace_back();
        range_info.primitiveCount = mb.index_count / 3u;
        range_info.primitiveOffset = (u32)((geom.index_offset + mb.index_offset) * sizeof(u32));
        range_info.firstVertex = geom.vertex_offset + mb.vertex_offset;
        range_info.transformOffset = 0;

        scratch_offset += scratch_sizes.at(i);
        ++i;
    }

    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> poffsets(ranges.size());
    for(u32 i = 0; i < ranges.size(); ++i) {
        poffsets.at(i) = &ranges.at(i);
    }

    auto cmd = frame_data.get().cmdpool->begin_onetime();
    vkCmdBuildAccelerationStructuresKHR(cmd, blas_geo_build_infos.size(), blas_geo_build_infos.data(), poffsets.data());
    frame_data.get().cmdpool->end(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkDeviceWaitIdle(dev);
}

void RendererVulkan::build_tlas() {
    std::vector<u32> tlas_mesh_offsets;
    std::vector<u32> blas_mesh_offsets;
    std::vector<u32> triangle_geo_inst_ids;
    std::vector<vks::AccelerationStructureInstanceKHR> tlas_instances;

    std::sort(blas_instances.begin(), blas_instances.end(),
              [](const BLASInstance& a, const BLASInstance& b) { return a.mesh_batch < b.mesh_batch; });

    // TODO : Compress mesh ids per triangle for identical blases with identical materials
    for(u32 i = 0, toff = 0, boff = 0; i < blas_instances.size(); ++i) {
        const BLASInstance& bi = blas_instances.at(i);
        const RenderMesh& mb = meshes.at(bi.mesh_batch);
        const RenderGeometry& geom = geometries.at(mb.geometry);
        const u32 mi_idx =
            std::distance(mesh_instances.begin(),
                          std::find_if(mesh_instances.begin(), mesh_instances.end(),
                                       [&bi](const MeshInstance& e) { return e.handle == bi.render_handle; }));
        const u32 scene_mi_idx = Engine::scene()->entity_node_idxs.at(mesh_instances.at(mi_idx).entity);

        triangle_geo_inst_ids.reserve(triangle_geo_inst_ids.size() + mb.index_count / 3u);
        for(u32 j = 0; j < mb.index_count / 3u; ++j) {
            triangle_geo_inst_ids.push_back(mi_idx);
        }

        vks::AccelerationStructureInstanceKHR& tlas_instance = tlas_instances.emplace_back();
        tlas_instance.transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(glm::mat4x3{
            Engine::scene()->get_final_transform(mesh_instances.at(mi_idx).entity) }));
        tlas_instance.instanceCustomIndex = 0;
        tlas_instance.mask = 0xFF;
        tlas_instance.instanceShaderBindingTableRecordOffset = 0;
        tlas_instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        tlas_instance.accelerationStructureReference = mb.metadata->blas_buffer.bda;

        tlas_mesh_offsets.push_back(toff);
        blas_mesh_offsets.push_back(boff / 3u);

        ++toff;
        boff += mb.index_count; // TODO: validate this
    }

    tlas_mesh_offsets_buffer = Buffer{ "tlas mesh offsets", tlas_mesh_offsets.size() * sizeof(tlas_mesh_offsets[0]),
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    tlas_mesh_offsets_buffer.push_data(tlas_mesh_offsets);
    blas_mesh_offsets_buffer = Buffer{ "blas mesh offsets", blas_mesh_offsets.size() * sizeof(blas_mesh_offsets[0]),
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    blas_mesh_offsets_buffer.push_data(blas_mesh_offsets);
    triangle_geo_inst_id_buffer =
        Buffer{ "triangle geo inst id", triangle_geo_inst_ids.size() * sizeof(triangle_geo_inst_ids[0]),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    triangle_geo_inst_id_buffer.push_data(triangle_geo_inst_ids);
    tlas_instance_buffer =
        Buffer{ "tlas_instance_buffer", sizeof(tlas_instances[0]) * tlas_instances.size(), 16u,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                false };
    tlas_instance_buffer.push_data(tlas_instances);

    vks::AccelerationStructureGeometryKHR geometry;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = false;
    geometry.geometry.instances.data.deviceAddress = tlas_instance_buffer.bda;

    vks::AccelerationStructureBuildGeometryInfoKHR tlas_info;
    tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    tlas_info.geometryCount = 1; // must be 1 for TLAS
    tlas_info.pGeometries = &geometry;
    tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    vks::AccelerationStructureBuildSizesInfoKHR build_size;
    const u32 max_primitives = tlas_instances.size();
    vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_info,
                                            &max_primitives, &build_size);

    tlas_buffer =
        Buffer{ "tlas_buffer", build_size.accelerationStructureSize,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false };

    vks::AccelerationStructureCreateInfoKHR acc_info;
    acc_info.buffer = tlas_buffer.buffer;
    acc_info.size = build_size.accelerationStructureSize;
    acc_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(dev, &acc_info, nullptr, &tlas);

    tlas_scratch_buffer = Buffer{ "tlas_scratch_buffer", build_size.buildScratchSize,
                                  rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    tlas_info.dstAccelerationStructure = tlas;
    tlas_info.scratchData.deviceAddress = tlas_scratch_buffer.bda;

    vks::AccelerationStructureBuildRangeInfoKHR build_range;
    build_range.primitiveCount = max_primitives;
    build_range.primitiveOffset = 0;
    build_range.firstVertex = 0;
    build_range.transformOffset = 0;
    VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };

    auto cmd = frame_data.get().cmdpool->begin_onetime();
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
    frame_data.get().cmdpool->end(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkDeviceWaitIdle(dev);
}

void RendererVulkan::refit_tlas() {
    assert(false && "TODO");
#if 0
    std::vector<const RenderModelInstance*> render_model_instances;
    render_model_instances.reserve(model_instances.size());

    for(auto& i : model_instances) {
        if(i.flags & InstanceFlags::RAY_TRACED_BIT) { render_model_instances.push_back(&i); }
    }

    std::vector<vks::AccelerationStructureInstanceKHR> instances(render_model_instances.size());
    for(u32 i = 0; i < instances.size(); ++i) {
        auto& instance = instances.at(i);
        instance.transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(render_model_instances.at(i)->transform));
        instance.instanceCustomIndex = 0;
        instance.mask = render_model_instances.at(i)->tlas_instance_mask;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = render_model_instances.at(i)->geom->metadata->blas_buffer.bda;
    }

    if(tlas_instance_buffer.capacity != instances.size() * sizeof(instances[0])) {
        ENG_WARN("Tlas instance buffer size differs from the instance vector in tlas update. That's probably an error: "
                 "{} != {}",
                 tlas_instance_buffer.capacity, render_model_instances.size() * sizeof(render_model_instances[0]));
    }

    tlas_instance_buffer.push_data(instances, 0);

    vks::AccelerationStructureGeometryKHR geometry;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = false;
    geometry.geometry.instances.data.deviceAddress = tlas_instance_buffer.bda;

    vks::AccelerationStructureBuildGeometryInfoKHR tlas_info;
    tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    tlas_info.geometryCount = 1;
    tlas_info.pGeometries = &geometry;
    tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    tlas_info.srcAccelerationStructure = tlas;
    tlas_info.dstAccelerationStructure = tlas;
    tlas_info.scratchData.deviceAddress = tlas_scratch_buffer.bda;

    const u32 max_primitives = instances.size();

    vks::AccelerationStructureBuildRangeInfoKHR build_range;
    build_range.primitiveCount = max_primitives;
    build_range.primitiveOffset = 0;
    build_range.firstVertex = 0;
    build_range.transformOffset = 0;
    VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };

    auto cmd = get_primitives().cmdpool.begin_onetime();
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
    get_primitives().cmdpool.end(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkDeviceWaitIdle(dev);
#endif
}

void RendererVulkan::update_ddgi() {
    // assert(false && "no allocating new pointers for images");

    if(!ddgi.debug_probes.empty()) { return; }

    BoundingBox scene_aabb;
    for(const Node& node : Engine::scene()->nodes) {
        if(!node.has_component<cmps::RenderMesh>()) { continue; }
        const cmps::RenderMesh& rm = Engine::ec()->get<cmps::RenderMesh>(node.handle);
        glm::mat4 t = Engine::scene()->get_final_transform(node.handle);
        BoundingBox m = rm.mesh->aabb;
        m.min = m.min * glm::mat4x3{ t };
        m.max = m.max * glm::mat4x3{ t };
        scene_aabb.min = glm::min(scene_aabb.min, m.min);
        scene_aabb.max = glm::max(scene_aabb.max, m.max);
    }

    ddgi.probe_dims = scene_aabb;
    const auto dim_scaling = glm::vec3{ 0.95, 0.8, 0.95 };
    ddgi.probe_dims.min *= dim_scaling;
    ddgi.probe_dims.max *= dim_scaling;
    ddgi.probe_distance = 1.3;

    ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
    ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
                          std::bit_ceil(ddgi.probe_counts.z) };
    // ddgi.probe_counts = {16, 4, 8};
    const auto num_probes = ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z;

    ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };
    // ddgi.probe_walk = {ddgi.probe_walk.x, 4.0f, ddgi.probe_walk.z};

    const u32 irradiance_texture_width = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const u32 irradiance_texture_height = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.z;
    const u32 visibility_texture_width = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const u32 visibility_texture_height = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.z;

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

    auto cmd = frame_data.get().cmdpool->begin_onetime();
    ddgi.radiance_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                             VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                             VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.irradiance_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                               VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.visibility_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                               VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                               VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.probe_offsets_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                                  VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
    frame_data.get().cmdpool->end(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });

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

    Handle<ModelAsset> sphere_handle = Engine::scene()->load_from_file("sphere/sphere.glb");
    ddgi.debug_probes.clear(); // TODO: also remove from the scene
    for(int i = 0; i < ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z; ++i) {
        // ddgi.debug_probes.push_back(Engine::scene()->instance_model(sphere_handle, { .transform = glm::mat4{ 1.0f } }));
    }

    vkQueueWaitIdle(gq);
}

Image* RendererVulkan::make_image(Image&& img) { return &images.emplace_back(std::move(img)); }

Buffer* RendererVulkan::make_buffer(Buffer&& buf) { return &buffers.emplace_back(std::move(buf)); }

VkSemaphore RendererVulkan::create_semaphore() {
    VkSemaphore sem;
    vks::SemaphoreCreateInfo info;
    VK_CHECK(vkCreateSemaphore(dev, &info, {}, &sem));
    return sem;
}

void RendererVulkan::destroy_semaphore(VkSemaphore sem) { vkDestroySemaphore(dev, sem, {}); }

VkShaderModule ShaderStorage::get_shader(const std::filesystem::path& path) {
    for(const auto& e : metadatas) {
        if(e.second.path == path) { return e.first; }
    }
    auto s = compile_shader(path);
    auto t = get_stage(path);
    metadatas[s] = ShaderMetadata{ .path = path, .stage = t };
    return s;
}

VkShaderStageFlagBits ShaderStorage::get_stage(std::filesystem::path path) const {
    if(path.extension() == ".glsl") { path.replace_extension(); }
    const auto ext = path.extension();
    if(ext == ".vert") { return VK_SHADER_STAGE_VERTEX_BIT; }
    if(ext == ".frag") { return VK_SHADER_STAGE_FRAGMENT_BIT; }
    if(ext == ".rgen") { return VK_SHADER_STAGE_RAYGEN_BIT_KHR; }
    if(ext == ".rchit") { return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; }
    if(ext == ".rmiss") { return VK_SHADER_STAGE_MISS_BIT_KHR; }
    if(ext == ".comp") { return VK_SHADER_STAGE_COMPUTE_BIT; }
    assert(false);
    return VK_SHADER_STAGE_VERTEX_BIT;
}

VkShaderModule ShaderStorage::compile_shader(std::filesystem::path path) {
    static const auto read_file = [](const std::filesystem::path& path) {
        std::string path_str = path.string();
        std::string path_to_includes = path.parent_path().string();

        char error[256] = {};
        char* parsed_file = stb_include_file(path_str.data(), nullptr, path_to_includes.data(), error);

        if(!parsed_file) {
            ENG_WARN("STBI_INCLUDE cannot parse file [{}]: {}", path_str, error);
            return std::string{};
        }

        std::string parsed_file_str{ parsed_file };
        free(parsed_file);
        return parsed_file_str;
    };
    const auto stage = get_stage(path);
    const auto kind = [stage] {
        if(stage == VK_SHADER_STAGE_VERTEX_BIT) { return shaderc_vertex_shader; }
        if(stage == VK_SHADER_STAGE_FRAGMENT_BIT) { return shaderc_fragment_shader; }
        if(stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) { return shaderc_raygen_shader; }
        if(stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) { return shaderc_closesthit_shader; }
        if(stage == VK_SHADER_STAGE_MISS_BIT_KHR) { return shaderc_miss_shader; }
        if(stage == VK_SHADER_STAGE_COMPUTE_BIT) { return shaderc_compute_shader; }
        assert(false);
        return shaderc_vertex_shader;
    }();

    path = ENGINE_BASE_ASSET_PATH / ("shaders" / path);

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetGenerateDebugInfo();
    // options.AddMacroDefinition("ASDF");
    shaderc::Compiler c;
    std::string file_str = read_file(path);
    const auto res = c.CompileGlslToSpv(file_str, kind, path.filename().string().c_str(), options);
    if(res.GetCompilationStatus() != shaderc_compilation_status_success) {
        ENG_WARN("Could not compile shader : {}, because : \"{}\"", path.string(), res.GetErrorMessage());
        return nullptr;
    }

    vks::ShaderModuleCreateInfo module_info;
    module_info.codeSize = (res.end() - res.begin()) * sizeof(u32);
    module_info.pCode = res.begin();
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(get_renderer().dev, &module_info, nullptr, &mod));
    return mod;
}

Fence::Fence(VkDevice dev, bool signaled) {
    vks::FenceCreateInfo info;
    if(signaled) { info.flags |= VK_FENCE_CREATE_SIGNALED_BIT; }
    VK_CHECK(vkCreateFence(dev, &info, nullptr, &fence));
}

Fence::Fence(Fence&& f) noexcept { *this = std::move(f); }

Fence& Fence::operator=(Fence&& f) noexcept {
    fence = std::exchange(f.fence, nullptr);
    return *this;
}

Fence::~Fence() noexcept { vkDestroyFence(get_renderer().dev, fence, nullptr); }

template <size_t frames> void Swapchain<frames>::create() {
    Window& window = *Engine::window();

    vks::SwapchainCreateInfoKHR sinfo;
    // sinfo.pNext = &format_list_info;
    // sinfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
    sinfo.surface = get_renderer().window_surface;
    sinfo.minImageCount = frames;
    sinfo.imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
    sinfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sinfo.imageExtent = VkExtent2D{ window.width, window.height };
    sinfo.imageArrayLayers = 1;
    sinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    sinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sinfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sinfo.clipped = true;

    if(swapchain) { vkDestroySwapchainKHR(get_renderer().dev, swapchain, nullptr); }
    VK_CHECK(vkCreateSwapchainKHR(get_renderer().dev, &sinfo, nullptr, &swapchain));
    std::array<VkImage, frames> imgs;
    u32 pframes = frames;
    VK_CHECK(vkGetSwapchainImagesKHR(get_renderer().dev, swapchain, &pframes, imgs.data()));

    for(u32 i = 0; i < frames; ++i) {
        images[i] = Image{ std::format("swapchain_image_{}", i),
                           imgs[i],
                           sinfo.imageExtent.width,
                           sinfo.imageExtent.height,
                           1,
                           1,
                           sinfo.imageArrayLayers,
                           sinfo.imageFormat,
                           VK_SAMPLE_COUNT_1_BIT,
                           sinfo.imageUsage };
    }
}

template <size_t frames>
u32 Swapchain<frames>::acquire(VkResult* res, u64 timeout, VkSemaphore semaphore, VkFence fence) {
    u32 idx;
    auto result = vkAcquireNextImageKHR(get_renderer().dev, swapchain, timeout, semaphore, fence, &idx);
    if(res) { *res = result; }
    return idx;
}

DescriptorPool::DescriptorPool(const PipelineLayout* layout, u32 max_sets) {
    std::unordered_map<VkDescriptorType, u32> idxs;
    std::vector<VkDescriptorPoolSize> sizes;
    for(u32 i = 0; i < layout->MAX_SETS; ++i) {
        auto& dl = layout->sets[i];
        for(u32 j = 0; j < dl.MAX_BINDINGS; ++j) {
            auto type = dl.bindings[j].get_vktype();
            if(type == VK_DESCRIPTOR_TYPE_MAX_ENUM) { continue; }
            u32 idx;
            if(!idxs.contains(type)) {
                idxs[type] = sizes.size();
                sizes.push_back({});
            }
            idx = idxs.at(type);
            sizes[idx].type = type;
            sizes[idx].descriptorCount += dl.bindings[j].count * max_sets;
        }
    }
    vks::DescriptorPoolCreateInfo info;
    info.maxSets = max_sets;
    info.poolSizeCount = sizes.size();
    info.pPoolSizes = sizes.data();
    VK_CHECK(vkCreateDescriptorPool(get_renderer().dev, &info, nullptr, &pool));
}

void DescriptorPool::allocate(const VkDescriptorSetLayout* layouts, VkDescriptorSet** sets, u32 count, std::span<u32> variable_count) {
    vks::DescriptorSetVariableDescriptorCountAllocateInfo var_info;
    var_info.descriptorSetCount = variable_count.size();
    var_info.pDescriptorCounts = variable_count.data();
    vks::DescriptorSetAllocateInfo info;
    info.descriptorPool = pool;
    info.descriptorSetCount = count;
    info.pSetLayouts = layouts;
    info.pNext = &var_info;
    std::vector<VkDescriptorSet> _sets(count);
    VK_CHECK(vkAllocateDescriptorSets(get_renderer().dev, &info, _sets.data()));
    for(u32 i = 0; i < count; ++i) {
        this->sets.push_back(_sets.at(i));
        sets[i] = &this->sets.back();
    }
}

void DescriptorPool::reset() {
    VK_CHECK(vkResetDescriptorPool(get_renderer().dev, pool, {}));
    for(auto& e : sets) {
        e = nullptr;
    }
}

RenderPass::RenderPass(const Pipeline* pipeline, DescriptorPool* desc_pool) : pipeline(pipeline), desc_pool(desc_pool) {
    assert(pipeline);
    assert(desc_pool);
}

void RenderPass::bind(VkCommandBuffer cmd) { vkCmdBindPipeline(cmd, pipeline->get_bindpoint(), pipeline->pipeline); }

void RenderPass::bind_desc_sets(VkCommandBuffer cmd) {
    for(u32 i = 0; i < sets.size(); ++i) {
        if(sets[i]) {
            vkCmdBindDescriptorSets(cmd, pipeline->get_bindpoint(), pipeline->layout->layout, i, 1, sets[i], 0, nullptr);
        }
    }
}

void RenderPass::update_desc_sets() {
    if(!pipeline || !desc_pool) {
        ENG_WARN("No pipeline or descriptor pool set!");
        return;
    }
    for(u32 i = 0; i < sets.size(); ++i) {
        auto& set = sets.at(i);
        auto& desc_layout = pipeline->layout->sets[i];

        if(desc_layout.is_empty()) { continue; }

        if(!set || !*set) {
            u32 variable_count = [&desc_layout] {
                if(desc_layout.variable_binding == -1) { return 0ull; }
                return std::visit(Visitor{ [](const std::vector<Image>* imgs) { return imgs->size(); },
                                           [](auto&&) {
                                               assert(false);
                                               return 0ull;
                                           } },
                                  desc_layout.bindings.at(desc_layout.variable_binding).res);
            }();
            desc_pool->allocate(&desc_layout.layout, &set, 1, { &variable_count, 1 });
            assert(*set);
        }

        u32 write_count = 0;
        std::array<vks::WriteDescriptorSet, DescriptorLayout::MAX_BINDINGS> writes{};
        std::array<std::variant<VkDescriptorBufferInfo, VkDescriptorImageInfo, vks::WriteDescriptorSetAccelerationStructureKHR>, DescriptorLayout::MAX_BINDINGS> write_infos{};
        std::vector<VkDescriptorImageInfo> variable_writes;

        for(u32 j = 0; j < desc_layout.bindings.size(); ++j) {
            const auto& binding = desc_layout.bindings.at(j);
            const auto vktype = binding.get_vktype();
            if(vktype == VK_DESCRIPTOR_TYPE_MAX_ENUM) { continue; }
            auto& write = writes.at(write_count);
            auto& write_info = write_infos.at(write_count);
            write.dstSet = *set;
            write.dstBinding = j;
            write.dstArrayElement = 0;
            write.descriptorCount = binding.count; // TODO: writes is too small if binding count is > 1
            write.descriptorType = vktype;
            std::visit(Visitor{
                           [&write_info](std::monostate) { assert(false); },
                           [&write, &write_info](const Buffer* e) {
                               write.pBufferInfo = &write_info.emplace<VkDescriptorBufferInfo>(VkDescriptorBufferInfo{
                                   .buffer = e->buffer, .offset = 0, .range = VK_WHOLE_SIZE });
                           },
                           [this, &write, &write_info, &binding](const Image* e) {
                               write.pImageInfo = &write_info.emplace<VkDescriptorImageInfo>(VkDescriptorImageInfo{
                                   .sampler = binding.sampler ? *binding.sampler : nullptr,
                                   .imageView = e->view,
                                   .imageLayout = binding.layout });
                           },
                           [&write, &write_info](VkAccelerationStructureKHR* e) {
                               vks::WriteDescriptorSetAccelerationStructureKHR i{};
                               i.accelerationStructureCount = 1;
                               i.pAccelerationStructures = e;
                               write.pNext = &write_info.emplace<vks::WriteDescriptorSetAccelerationStructureKHR>(i);
                           },
                           [&write, &write_info, &variable_writes, &binding](const std::vector<Image>* e) {
                               variable_writes.reserve(e->size());
                               for(auto& img : *e) {
                                   variable_writes.push_back(VkDescriptorImageInfo{
                                       .sampler = *binding.sampler, .imageView = img.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
                               }
                               write.descriptorCount = e->size();
                               write.pImageInfo = variable_writes.data();
                           },
                       },
                       binding.res);
            ++write_count;
        }
        vkUpdateDescriptorSets(get_renderer().dev, write_count, writes.data(), 0, nullptr);
    }
}

void RenderPass::push_constant(VkCommandBuffer cmd, u32 offset, u32 size, const void* value) {
    vkCmdPushConstants(cmd, pipeline->layout->layout, VK_SHADER_STAGE_ALL, offset, size, value);
}

Semaphore::Semaphore(VkDevice dev, bool timeline) {
    vks::SemaphoreCreateInfo info;
    vks::SemaphoreTypeCreateInfo tinfo;
    tinfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    if(timeline) { info.pNext = &tinfo; }
    VK_CHECK(vkCreateSemaphore(dev, &info, nullptr, &semaphore));
}

Semaphore::Semaphore(Semaphore&& o) noexcept { *this = std::move(o); }

Semaphore& Semaphore::operator=(Semaphore&& o) noexcept {
    semaphore = std::exchange(o.semaphore, nullptr);
    return *this;
}

Semaphore::~Semaphore() noexcept {
    vkDestroySemaphore(get_renderer().dev, semaphore, nullptr);
    semaphore = nullptr;
}

DescriptorBinding::DescriptorBinding(Resource res, u32 count, VkImageLayout layout, std::optional<VkSampler> sampler)
    : res(res), layout(layout), sampler(sampler), count(count) {}

DescriptorBinding::DescriptorBinding(Resource res, u32 count, std::optional<VkSampler> sampler)
    : DescriptorBinding(res, count, deduce_layout(res, sampler), sampler) {}

DescriptorBinding::DescriptorBinding(Resource res) : DescriptorBinding(res, 1) {}

VkDescriptorType DescriptorBinding::get_vktype() const {
    if(res.index() != 0 && sampler) {
        if(*sampler == nullptr) {
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        } else {
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }
    }

    return std::visit(Visitor{
                          [](std::monostate) { return VK_DESCRIPTOR_TYPE_MAX_ENUM; },
                          [](const Buffer* e) {
                              if(e->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
                                  return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                              } else if(e->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
                                  return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                              } else {
                                  assert(false);
                                  return VK_DESCRIPTOR_TYPE_MAX_ENUM;
                              }
                          },
                          [](const Image* e) {
                              if(e->usage & VK_IMAGE_USAGE_STORAGE_BIT) { return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; }
                              assert(false);
                              return VK_DESCRIPTOR_TYPE_MAX_ENUM;
                          },
                          [](VkAccelerationStructureKHR*) { return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; },
                          [](const std::vector<Image>* e) {
                              assert(false);
                              return VK_DESCRIPTOR_TYPE_MAX_ENUM;
                          },
                      },
                      res);
}

VkImageLayout DescriptorBinding::deduce_layout(const Resource& res, const std::optional<VkSampler>& sampler) {
    return std::visit(Visitor{ [&sampler](const Image* e) {
                                  if(sampler) { return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL; }
                                  if(e->usage & VK_IMAGE_USAGE_STORAGE_BIT) { return VK_IMAGE_LAYOUT_GENERAL; }
                                  return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
                              },
                               [&sampler](const std::vector<Image>*) {
                                   if(sampler) { return VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL; }
                                   return VK_IMAGE_LAYOUT_GENERAL;
                               },
                               [](auto&&) { return VK_IMAGE_LAYOUT_MAX_ENUM; } },
                      res);
}

bool DescriptorLayout::is_empty() const { return bindings[0].res.index() == 0; }

void QueueSubmit::submit(VkQueue queue, QueueSubmision submissions, Fence* fence) {
    submit(queue, std::span{ &submissions, 1 }, fence);
}

void QueueSubmit::submit(VkQueue queue, std::span<QueueSubmision> submissions, Fence* fence) {
    std::vector<vks::SubmitInfo2> subs(submissions.size());
    std::vector<std::vector<vks::CommandBufferSubmitInfo>> cmdsubs(submissions.size());
    std::vector<std::vector<vks::SemaphoreSubmitInfo>> waitsubs(submissions.size());
    std::vector<std::vector<vks::SemaphoreSubmitInfo>> signalsubs(submissions.size());
    for(u32 i = 0; i < submissions.size(); ++i) {
        auto& sub = subs[i];
        auto& isub = submissions[i];
        sub.commandBufferInfoCount = isub.cmds.size();
        cmdsubs[i].resize(sub.commandBufferInfoCount);
        for(u32 j = 0; j < sub.commandBufferInfoCount; ++j) {
            cmdsubs[i][j].commandBuffer = isub.cmds[j];
        }
        sub.pCommandBufferInfos = cmdsubs[i].data();

        sub.waitSemaphoreInfoCount = isub.wait_sems.size();
        waitsubs[i].resize(sub.waitSemaphoreInfoCount);
        for(u32 j = 0; j < sub.waitSemaphoreInfoCount; ++j) {
            waitsubs[i][j].semaphore = isub.wait_sems[j].first->semaphore;
            waitsubs[i][j].value = isub.wait_sems[j].second;
            waitsubs[i][j].stageMask = isub.wait_stages[j];
        }
        sub.pWaitSemaphoreInfos = waitsubs[i].data();

        sub.signalSemaphoreInfoCount = isub.signal_sems.size();
        signalsubs[i].resize(sub.signalSemaphoreInfoCount);
        for(u32 j = 0; j < sub.signalSemaphoreInfoCount; ++j) {
            signalsubs[i][j].semaphore = isub.signal_sems[j].first->semaphore;
            signalsubs[i][j].value = isub.signal_sems[j].second;
            signalsubs[i][j].stageMask = isub.signal_stages[j];
        }
        sub.pSignalSemaphoreInfos = signalsubs[i].data();
    }

    vkQueueSubmit2(queue, subs.size(), subs.data(), fence ? fence->fence : nullptr);
}
