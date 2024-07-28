#include <volk/volk.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include <vk-bootstrap/src/VkBootstrap.h>
#include <shaderc/shaderc.hpp>
// TODO: This shouldn't be here
#define STB_INCLUDE_IMPLEMENTATION
#define STB_INCLUDE_LINE_GLSL
#include <stb/stb_include.h>
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"
#include <numeric>
#include <fstream>

#define VK_CHECK(func)                                                                                                 \
    if(const auto res = func; res != VK_SUCCESS) {                                                                     \
        ENG_RTERROR(std::format("[VK][ERROR][{} : {}] ({})", __FILE__, __LINE__, #func));                              \
    }

Buffer::Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::BufferCreateInfo binfo;
    VmaAllocationCreateInfo vinfo{};
    const auto use_bda = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) > 0;

    binfo.size = size;
    binfo.usage = usage;

    vinfo.usage = VMA_MEMORY_USAGE_AUTO;
    if(map) {
        vinfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    if(use_bda) { vinfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; }

    VmaAllocationInfo vainfo{};
    if(vmaCreateBuffer(renderer->vma, &binfo, &vinfo, &buffer, &alloc, &vainfo) != VK_SUCCESS) {
        throw std::runtime_error{ "Could not create buffer" };
    }

    mapped = vainfo.pMappedData;

    if(use_bda) {
        vks::BufferDeviceAddressInfo bdainfo;
        bdainfo.buffer = buffer;
        bda = vkGetBufferDeviceAddress(renderer->dev, &bdainfo);
    }

    capacity = size;
    set_debug_name(buffer, name);

    ENG_LOG("ALLOCATING BUFFER {} OF SIZE {:.2f} KB", name.c_str(), static_cast<float>(size) / 1024.0f);
}

Buffer::Buffer(const std::string& name, size_t size, size_t alignment, VkBufferUsageFlags usage, bool map) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::BufferCreateInfo binfo;
    VmaAllocationCreateInfo vinfo{};
    const auto use_bda = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) > 0;

    binfo.size = size;
    binfo.usage = usage;

    vinfo.usage = VMA_MEMORY_USAGE_AUTO;
    if(map) {
        vinfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    if(use_bda) { vinfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; }

    VmaAllocationInfo vainfo{};
    if(vmaCreateBufferWithAlignment(renderer->vma, &binfo, &vinfo, alignment, &buffer, &alloc, &vainfo) != VK_SUCCESS) {
        throw std::runtime_error{ "Could not create buffer" };
    }

    mapped = vainfo.pMappedData;

    if(use_bda) {
        vks::BufferDeviceAddressInfo bdainfo;
        bdainfo.buffer = buffer;
        bda = vkGetBufferDeviceAddress(renderer->dev, &bdainfo);
    }

    capacity = size;
    set_debug_name(buffer, name);

    ENG_LOG("ALLOCATING BUFFER {} OF SIZE {:.2f} KB", name.c_str(), static_cast<float>(size) / 1024.0f);
}

size_t Buffer::push_data(std::span<const std::byte> data) {
    if(!mapped) {
        ENG_WARN("Trying to push to an unmapped buffer");
        return 0;
    }

    const auto free_space = get_free_space();
    const auto size_to_copy = data.size_bytes() > free_space ? free_space : data.size_bytes();

    if(size_to_copy < data.size_bytes()) {
        ENG_WARN("Buffer is too small for the incoming data: %ull <- %ull", free_space, size_to_copy);
        return 0;
    }

    std::memcpy(static_cast<std::byte*>(mapped) + size, data.data(), size_to_copy);

    size += size_to_copy;

    return size_to_copy;
}

void RendererVulkan::init() {
    initialize_vulkan();
    create_swapchain();
    create_rt_output_image();
    compile_shaders();
    build_rtpp();
    build_sbt();
}

void RendererVulkan::initialize_vulkan() {
    if(volkInitialize() != VK_SUCCESS) { throw std::runtime_error{ "Volk loader not found. Stopping" }; }

    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
                        .enable_validation_layers()
                        .use_default_debug_messenger()
                        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
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
                        //.add_required_extension(VK_KHR_MAINTENANCE_5_EXTENSION_NAME)
                        .select();
    if(!phys_ret) { throw std::runtime_error{ "Failed to select Vulkan Physical Device. Error: " }; }

    vkb::DeviceBuilder device_builder{ phys_ret.value() };

    vks::PhysicalDeviceSynchronization2Features synch2_features;
    synch2_features.synchronization2 = true;

    vks::PhysicalDeviceHostQueryResetFeatures query_reset_features;
    query_reset_features.hostQueryReset = true;

    vks::PhysicalDeviceDynamicRenderingFeatures dyn_features;
    dyn_features.dynamicRendering = true;

    vks::PhysicalDeviceDescriptorIndexingFeatures desc_features;
    desc_features.descriptorBindingVariableDescriptorCount = true;
    desc_features.descriptorBindingPartiallyBound = true;
    desc_features.shaderSampledImageArrayNonUniformIndexing = true;
    desc_features.shaderStorageImageArrayNonUniformIndexing = true;
    desc_features.descriptorBindingSampledImageUpdateAfterBind = true;
    desc_features.descriptorBindingStorageImageUpdateAfterBind = true;
    desc_features.descriptorBindingUniformBufferUpdateAfterBind = true;
    desc_features.descriptorBindingStorageBufferUpdateAfterBind = true;
    desc_features.descriptorBindingUpdateUnusedWhilePending = true;
    desc_features.runtimeDescriptorArray = true;

    vks::PhysicalDeviceFeatures2 dev_2_features;
    dev_2_features.features = {
        .geometryShader = true,
        .multiDrawIndirect = true,
        .fragmentStoresAndAtomics = true,
    };

    vks::PhysicalDeviceBufferDeviceAddressFeatures bda_features;
    bda_features.bufferDeviceAddress = true;

    vks::PhysicalDeviceAccelerationStructureFeaturesKHR acc_features;
    acc_features.accelerationStructure = true;
    acc_features.descriptorBindingAccelerationStructureUpdateAfterBind = true;

    vks::PhysicalDeviceRayTracingPipelineFeaturesKHR rtpp_features;
    rtpp_features.rayTracingPipeline = true;
    rtpp_features.rayTraversalPrimitiveCulling = true;

    vks::PhysicalDeviceScalarBlockLayoutFeatures scalar_features;
    scalar_features.scalarBlockLayout = true;

    vks::PhysicalDeviceMaintenance5FeaturesKHR maint5_features;
    maint5_features.maintenance5 = true;

    auto dev_ret = device_builder.add_pNext(&dev_2_features)
                       .add_pNext(&desc_features)
                       .add_pNext(&dyn_features)
                       .add_pNext(&query_reset_features)
                       .add_pNext(&synch2_features)
                       .add_pNext(&bda_features)
                       .add_pNext(&acc_features)
                       .add_pNext(&rtpp_features)
                       .add_pNext(&scalar_features)
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

    VmaAllocator allocator;
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &allocator));
    vma = allocator;

    vks::CommandPoolCreateInfo cmdpi;
    cmdpi.queueFamilyIndex = gqi;

    VK_CHECK(vkCreateCommandPool(device, &cmdpi, nullptr, &cmdpool));

    ubo = Buffer{ "ubo", 1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true };
    constexpr size_t ONE_MB = 1024 * 1024;
    vertex_buffer = Buffer{ "vertex_buffer", ONE_MB,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            true };
    index_buffer = Buffer{ "index_buffer", ONE_MB,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           true };

    vks::SemaphoreCreateInfo sem_swapchain_info;
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_swapchain_image));
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_tracing_done));
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_copy_to_sw_img_done));
}

void RendererVulkan::create_swapchain() {
    vks::SwapchainCreateInfoKHR sinfo;
    sinfo.surface = window_surface;
    sinfo.minImageCount = 2;
    sinfo.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
    sinfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sinfo.imageExtent = VkExtent2D{ Engine::window()->size[0], Engine::window()->size[1] };
    sinfo.imageArrayLayers = 1;
    sinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sinfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sinfo.clipped = true;

    VK_CHECK(vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain));
    uint32_t num_images;
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, nullptr);

    std::vector<VkImage> images(num_images);
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, images.data());

    swapchain_images = images;
    swapchain_format = sinfo.imageFormat;
}

template <typename T> static T align_up(T a, T b) { return (a + b - 1) & -b; }

void RendererVulkan::render() {
    reset_command_pool(cmdpool);
    if(flags & VkRendererFlags::DIRTY_UPLOADS) {
        upload_staged_models();
        flags ^= VkRendererFlags::DIRTY_UPLOADS;
    }
    if(flags & VkRendererFlags::DIRTY_BLAS) {
        build_blas();
        flags ^= VkRendererFlags::DIRTY_BLAS;
    }
    if(flags & VkRendererFlags::DIRTY_INSTANCES) {
        upload_instances();
        flags ^= VkRendererFlags::DIRTY_INSTANCES;
    }
    if(flags & VkRendererFlags::DIRTY_TLAS) {
        build_tlas();
        prepare_ddgi();
        build_desc_sets();
        flags ^= VkRendererFlags::DIRTY_TLAS;
    }

    vks::AcquireNextImageInfoKHR acq_info;
    uint32_t sw_img_idx;
    vkAcquireNextImageKHR(dev, swapchain, -1ULL, primitives.sem_swapchain_image, nullptr, &sw_img_idx);

    auto raytrace_cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    vkCmdBindPipeline(raytrace_cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytracing_pipeline);
    vkCmdBindDescriptorSets(raytrace_cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytracing_layout, 0, 1,
                            &raytracing_set, 0, nullptr);

    const uint32_t handle_size_aligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);

    vks::StridedDeviceAddressRegionKHR raygen_sbt;
    raygen_sbt.deviceAddress = sbt.bda;
    raygen_sbt.size = handle_size_aligned * 1;
    raygen_sbt.stride = handle_size_aligned;

    vks::StridedDeviceAddressRegionKHR miss_sbt;
    miss_sbt.deviceAddress = sbt.bda;
    miss_sbt.size = handle_size_aligned * 2;
    miss_sbt.stride = handle_size_aligned;

    vks::StridedDeviceAddressRegionKHR hit_sbt;
    hit_sbt.deviceAddress = sbt.bda;
    hit_sbt.size = handle_size_aligned * 1;
    hit_sbt.stride = handle_size_aligned;

    vks::StridedDeviceAddressRegionKHR callable_sbt;

    VkClearColorValue clear_value{ 0.0f, 0.0f, 0.0f, 1.0f };
    VkImageSubresourceRange clear_range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };
    const auto* window = Engine::window();
    uint32_t push_offset = 0;
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_offset,
                       sizeof(combined_rt_buffers_buffer.bda), &combined_rt_buffers_buffer.bda);
    push_offset += sizeof(VkDeviceAddress);
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_offset, sizeof(VkDeviceAddress),
                       &vertex_buffer.bda);
    push_offset += sizeof(VkDeviceAddress);
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_offset, sizeof(VkDeviceAddress),
                       &index_buffer.bda);
    push_offset += sizeof(VkDeviceAddress);
    uint32_t mode = 1;
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_offset, sizeof(VkDeviceAddress), &ddgi_buffer.bda);
    push_offset += sizeof(VkDeviceAddress);
    const auto push_constant_mode_offset = push_offset;
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_constant_mode_offset, sizeof(uint32_t), &mode);

    vkCmdTraceRaysKHR(raytrace_cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, ddgi.rays_per_probe, ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z, 1);

    vks::ImageMemoryBarrier2 rt_to_comp_img_barrier;
    rt_to_comp_img_barrier.image = ddgi.radiance_texture.image;
    rt_to_comp_img_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    rt_to_comp_img_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    rt_to_comp_img_barrier.srcStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    rt_to_comp_img_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    rt_to_comp_img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    rt_to_comp_img_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    rt_to_comp_img_barrier.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };

    vks::DependencyInfo rt_to_comp_dep_info;
    rt_to_comp_dep_info.imageMemoryBarrierCount = 1;
    rt_to_comp_dep_info.pImageMemoryBarriers = &rt_to_comp_img_barrier;

    vkCmdPipelineBarrier2(raytrace_cmd, &rt_to_comp_dep_info);

    vkCmdBindPipeline(raytrace_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ddgi_compute_pipeline);
    vkCmdBindDescriptorSets(raytrace_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, raytracing_layout, 0, 1, &raytracing_set, 0, nullptr);

    mode = 0;
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_constant_mode_offset, sizeof(uint32_t), &mode);
    vkCmdDispatch(raytrace_cmd, std::ceilf((float)ddgi.irradiance_texture.width / 8u), std::ceilf((float)ddgi.irradiance_texture.height / 8u), 1u);

    mode = 1;
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_constant_mode_offset, sizeof(uint32_t), &mode);
    vkCmdDispatch(raytrace_cmd, std::ceilf((float)ddgi.visibility_texture.width / 8u), std::ceilf((float)ddgi.visibility_texture.height / 8u), 1u);

    rt_to_comp_img_barrier.image = ddgi.irradiance_texture.image;
    rt_to_comp_img_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    rt_to_comp_img_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    rt_to_comp_img_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    rt_to_comp_img_barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    rt_to_comp_img_barrier.dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    rt_to_comp_img_barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    rt_to_comp_img_barrier.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };

    vks::ImageMemoryBarrier2 ddgi_visibility_comp_to_rt_barrier = rt_to_comp_img_barrier;
    ddgi_visibility_comp_to_rt_barrier.image = ddgi.visibility_texture.image;

    vks::ImageMemoryBarrier2 ddgi_barriers[] {
        rt_to_comp_img_barrier, ddgi_visibility_comp_to_rt_barrier
    };
    rt_to_comp_dep_info.imageMemoryBarrierCount = sizeof(ddgi_barriers) / sizeof(ddgi_barriers[0]);
    rt_to_comp_dep_info.pImageMemoryBarriers = ddgi_barriers;
    vkCmdPipelineBarrier2(raytrace_cmd, &rt_to_comp_dep_info);
    rt_to_comp_dep_info.imageMemoryBarrierCount = 1; // just to be safe and i'm too lazy to check if i'm reusing this variable anywhere later

    vkCmdBindPipeline(raytrace_cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytracing_pipeline);
    mode = 0;
    vkCmdPushConstants(raytrace_cmd, raytracing_layout, VK_SHADER_STAGE_ALL, push_constant_mode_offset, sizeof(uint32_t), &mode);
    vkCmdTraceRaysKHR(raytrace_cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, window->size[0], window->size[1], 1);

    end_recording(raytrace_cmd);
    auto present_cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    vks::ImageMemoryBarrier2 sw_to_dst, sw_to_pres;
    sw_to_dst.image = swapchain_images.at(sw_img_idx);
    sw_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sw_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    sw_to_dst.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    sw_to_dst.srcAccessMask = VK_ACCESS_NONE;
    sw_to_dst.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    sw_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sw_to_dst.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
    };
    sw_to_pres = sw_to_dst;
    sw_to_pres.oldLayout = sw_to_dst.newLayout;
    sw_to_pres.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    sw_to_pres.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    sw_to_pres.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sw_to_pres.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    sw_to_pres.dstAccessMask = VK_ACCESS_NONE;

    vks::DependencyInfo sw_dep_info;
    sw_dep_info.imageMemoryBarrierCount = 1;
    sw_dep_info.pImageMemoryBarriers = &sw_to_dst;
    vkCmdPipelineBarrier2(present_cmd, &sw_dep_info);

    vks::ImageCopy img_copy;
    img_copy.srcOffset = {};
    img_copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    img_copy.dstOffset = {};
    img_copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    img_copy.extent = { window->size[0], window->size[1], 1 };
    vkCmdCopyImage(present_cmd, rt_image.image, VK_IMAGE_LAYOUT_GENERAL, sw_to_dst.image, sw_to_dst.newLayout, 1, &img_copy);

    sw_dep_info.pImageMemoryBarriers = &sw_to_pres;
    vkCmdPipelineBarrier2(present_cmd, &sw_dep_info);
    end_recording(present_cmd);

    submit_recordings(
        gq, { RecordingSubmitInfo{ .buffers = { raytrace_cmd },
                                   .signals = { { primitives.sem_tracing_done, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR } } },
              RecordingSubmitInfo{ .buffers = { present_cmd },
                                   .waits = { { primitives.sem_tracing_done, VK_PIPELINE_STAGE_2_TRANSFER_BIT },
                                              { primitives.sem_swapchain_image, VK_PIPELINE_STAGE_2_TRANSFER_BIT } },
                                   .signals = { { primitives.sem_copy_to_sw_img_done, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT } } } });

    vks::PresentInfoKHR pinfo;
    pinfo.swapchainCount = 1;
    pinfo.pSwapchains = &swapchain;
    pinfo.pImageIndices = &sw_img_idx;
    pinfo.waitSemaphoreCount = 1;
    pinfo.pWaitSemaphores = &primitives.sem_copy_to_sw_img_done;
    vkQueuePresentKHR(gq, &pinfo);
    vkQueueWaitIdle(gq);
}

HandleBatchedModel RendererVulkan::batch_model(ImportedModel& model, BatchSettings settings) {
    flags |= VkRendererFlags::DIRTY_UPLOADS;

    const auto total_vertices = get_total_vertices();
    const auto total_indices = get_total_indices();
    const auto total_meshes = static_cast<uint32_t>(meshes.size());
    const auto total_materials = static_cast<uint32_t>(materials.size());
    const auto total_textures = static_cast<uint32_t>(upload_images.size() + textures.size());
    const auto total_models = static_cast<uint32_t>(models.size());

    models.push_back(RenderModel{ .flags = {},
                                  .first_vertex = total_vertices,
                                  .vertex_count = (uint32_t)model.vertices.size(),
                                  .first_index = total_indices,
                                  .index_count = (uint32_t)model.indices.size(),
                                  .first_mesh = total_meshes,
                                  .mesh_count = (uint32_t)model.meshes.size(),
                                  .first_material = total_materials,
                                  .material_count = (uint32_t)model.materials.size(),
                                  .first_texture = total_textures,
                                  .texture_count = (uint32_t)model.textures.size() });

    RenderModel& render_model = models.back();

    if(settings.flags & BatchFlags::RAY_TRACED_BIT) {
        flags |= VkRendererFlags::DIRTY_BLAS;
        render_model.flags |= VkRendererFlags::DIRTY_BLAS;
    }

    for(auto& mesh : model.meshes) {
        meshes.push_back(RenderMesh{ .vertex_offset = mesh.vertex_offset,
                                     .vertex_count = mesh.vertex_count,
                                     .index_offset = mesh.index_offset,
                                     .index_count = mesh.index_count,
                                     .material = mesh.material ? *mesh.material : 0 });
    }

    for(auto& mat : model.materials) {
        materials.push_back(RenderMaterial{ .color_texture = mat.color_texture, .normal_texture = mat.normal_texture });
    }

    for(auto& tex : model.textures) {
        upload_images.push_back(UploadImage{
            .name = tex.name, .width = tex.size.first, .height = tex.size.second, .rgba_data = tex.rgba_data });
    }

    upload_vertices.resize(upload_vertices.size() + model.vertices.size());
    upload_indices.resize(upload_indices.size() + model.indices.size());

    std::transform(model.vertices.begin(), model.vertices.end(), upload_vertices.end() - model.vertices.size(),
                   [](const ImportedModel::Vertex& v) { return Vertex{ .pos = v.pos, .nor = v.nor, .uv = v.uv }; });

    // undoing relative indices - they are now absolute (correctly, without any offsets, index the big vertex buffer with other models)
    for(uint32_t i = 0, idx = 0, offset = upload_indices.size() - model.indices.size(); i < model.meshes.size(); ++i) {
        const auto& mesh = model.meshes.at(i);
        for(uint32_t j = 0; j < mesh.index_count; ++j) {
            upload_indices.at(offset + idx) = total_vertices + mesh.vertex_offset + model.indices.at(idx);
            ++idx;
        }
    }

    rt_metadata.emplace_back();

    // clang-format off
    ENG_LOG("Batching model {}: [VXS: {:.2f} KB, IXS: {:.2f} KB, Textures: {:.2f} KB]", model.name,
            static_cast<float>(model.vertices.size() * sizeof(model.vertices[0])) / 1000.0f,
            static_cast<float>(model.indices.size() * sizeof(model.indices[0]) / 1000.0f),
            static_cast<float>(std::accumulate(model.textures.begin(), model.textures.end(), 0ull, [](uint64_t sum, const ImportedModel::Texture& tex) { return sum + tex.size.first * tex.size.second * 4u; })) / 1000.0f);
    // clang-format on

    return HandleBatchedModel{ total_models };
}

HandleInstancedModel RendererVulkan::instance_model(HandleBatchedModel model, InstanceSettings settings) {
    model_instances.push_back(RenderModelInstance{
        .model = Handle<RenderModel>{ *model },
        .flags = settings.flags,
        .transform = settings.transform,
    });

    flags |= VkRendererFlags::DIRTY_INSTANCES;
    if(settings.flags & InstanceFlags::RAY_TRACED_BIT) { flags |= VkRendererFlags::DIRTY_TLAS; }

    return HandleInstancedModel{ (uint32_t)model_instances.size() - 1u };
}

void RendererVulkan::upload_model_textures() {
    const auto total_tex_size =
        std::accumulate(upload_images.begin(), upload_images.end(), 0ull,
                        [](uint64_t sum, const UploadImage& tex) { return sum + tex.width * tex.height * 4ull; });

    Buffer texture_staging_buffer{ "texture staging buffer", total_tex_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true };

    std::vector<Image> dst_textures;
    std::vector<vks::CopyBufferToImageInfo2> texture_copy_datas;
    std::vector<vks::BufferImageCopy2> buffer_copies;

    dst_textures.reserve(upload_images.size());
    texture_copy_datas.reserve(upload_images.size());
    buffer_copies.reserve(upload_images.size());

    uint64_t texture_byte_offset = 0;
    for(const auto& tex : upload_images) {
        auto& texture = dst_textures.emplace_back(std::format("{}", tex.name), tex.width, tex.height, 1, 1, 1,
                                                  VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT,
                                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vks::BufferImageCopy2& region = buffer_copies.emplace_back();
        region.bufferOffset = texture_byte_offset;
        region.imageExtent = { .width = tex.width, .height = tex.height, .depth = 1 };
        region.imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        vks::CopyBufferToImageInfo2& copy = texture_copy_datas.emplace_back();
        copy.srcBuffer = texture_staging_buffer.buffer;
        copy.dstImage = texture.image;
        copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy.regionCount = 1;
        copy.pRegions = &region;

        texture_staging_buffer.push_data(std::as_bytes(std::span{ tex.rgba_data }));
        texture_byte_offset += tex.width * tex.height * 4ull;
    }

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    for(uint32_t i = 0; i < texture_copy_datas.size(); ++i) {
        auto& copy = texture_copy_datas.at(i);
        auto& texture = dst_textures.at(i);

        texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                  VK_ACCESS_2_TRANSFER_WRITE_BIT, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage2(cmd, &copy);
        texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                  false, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }

    submit_recording(gq, cmd);
    vkQueueWaitIdle(gq);
    textures.insert(textures.end(), std::make_move_iterator(dst_textures.begin()), std::make_move_iterator(dst_textures.end()));
}
void RendererVulkan::upload_staged_models() {
    upload_model_textures();

    vertex_buffer.push_data(std::as_bytes(std::span{ upload_vertices }));
    index_buffer.push_data(std::as_bytes(std::span{ upload_indices }));

    upload_vertices = {};
    upload_indices = {};
    upload_images = {};
}

void RendererVulkan::upload_instances() {
    std::sort(model_instances.begin(), model_instances.end(),
              [](const RenderModelInstance& a, const RenderModelInstance& b) { return a.model < b.model; });

    const auto total_triangles = get_total_triangles();
    std::vector<uint32_t> per_triangle_mesh_ids;
    std::vector<uint32_t> per_tlas_instance_triangle_offsets;
    std::vector<GPURenderMeshData> render_mesh_data;
    per_triangle_mesh_ids.reserve(total_triangles);
    per_tlas_instance_triangle_offsets.reserve(model_instances.size());
    render_mesh_data.reserve(model_instances.size() * meshes.size());

    for(const RenderModel& model : models) {
        for(uint32_t i = 0u; i < model.mesh_count; ++i) {
            const RenderMesh& mesh = meshes.at(model.first_mesh + i);
            const RenderMaterial& material = materials.at(model.first_material + mesh.material);

            render_mesh_data.push_back(GPURenderMeshData{ .color_texture_idx =
                                                              model.first_texture + material.color_texture.value_or(0u) });

            for(uint32_t j = 0u; j < mesh.index_count / 3u; ++j) {
                per_triangle_mesh_ids.push_back(model.first_mesh + i);
            }
        }
    }

    for(const RenderModelInstance& instance : model_instances) {
        const RenderModel& model = models.at(*instance.model);
        per_tlas_instance_triangle_offsets.push_back(model.first_index / 3u);
    }

    // clang-format off
    per_triangle_mesh_id_buffer = Buffer{"per triangle mesh id", per_triangle_mesh_ids.size() * sizeof(per_triangle_mesh_ids[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true};
    per_tlas_triangle_offsets_buffer = Buffer{"per tlas triangle offsets", per_tlas_instance_triangle_offsets.size() * sizeof(per_tlas_instance_triangle_offsets[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true};
    render_mesh_data_buffer = Buffer{"render mesh data", render_mesh_data.size() * sizeof(render_mesh_data[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true};
    combined_rt_buffers_buffer = Buffer{"combined rt buffers", sizeof(VkDeviceAddress[3]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true};

    per_triangle_mesh_id_buffer.push_data(std::as_bytes(std::span{per_triangle_mesh_ids}));
    per_tlas_triangle_offsets_buffer.push_data(std::as_bytes(std::span{per_tlas_instance_triangle_offsets}));
    render_mesh_data_buffer.push_data(std::as_bytes(std::span{render_mesh_data}));

    std::vector<VkDeviceAddress> combined_rt_buffers{ per_triangle_mesh_id_buffer.bda, per_tlas_triangle_offsets_buffer.bda, render_mesh_data_buffer.bda };
    combined_rt_buffers_buffer.push_data(std::as_bytes(std::span{combined_rt_buffers}));
    // clang-format on

    /*
        per tlas instance triangle offsets
        m1 m2 m1 m2 m1
        0  30 0  30 0
    */

    /*
    mesh_ids[triangle_offsets[gl_InstanceID] + gl_PrimitiveID]
    (triangle_offsets[gl_InstanceID] + gl_PrimitiveID) * 3 + 0
    (triangle_offsets[gl_InstanceID] + gl_PrimitiveID) * 3 + 1
    (triangle_offsets[gl_InstanceID] + gl_PrimitiveID) * 3 + 2
    */
}

void RendererVulkan::compile_shaders() {
    shaderc::Compiler c;

    static const auto read_file = [](const std::filesystem::path& path) {
        std::string path_str = path.string();
        std::string path_to_includes = path.parent_path().string();

        char error[256];

        char* parsed_file = stb_include_file(path_str.data(), nullptr, path_to_includes.data(), error);
        std::string parsed_file_str{ parsed_file };
        free(parsed_file);
        return parsed_file_str;
    };

    std::filesystem::path files[]{
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "closest_hit.rchit",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss2.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "raygen.rgen",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "probe_irradiance.comp",
    };
    shaderc_shader_kind kinds[]{ shaderc_closesthit_shader, shaderc_miss_shader, shaderc_miss_shader,
                                 shaderc_raygen_shader, shaderc_compute_shader };
    VkShaderStageFlagBits stages[]{ VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
                                    VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_COMPUTE_BIT };
    ShaderModuleType types[]{ ShaderModuleType::RT_BASIC_CLOSEST_HIT, ShaderModuleType::RT_BASIC_MISS, ShaderModuleType::RT_BASIC_MISS2,
                              ShaderModuleType::RT_BASIC_RAYGEN, ShaderModuleType::RT_BASIC_PROBE_IRRADIANCE_COMPUTE };
    std::vector<std::vector<uint32_t>> compiled_modules;

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetGenerateDebugInfo();
    // options.AddMacroDefinition("ASDF");
    for(int i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        std::string file_str = read_file(files[i]);
        auto res = c.CompileGlslToSpv(file_str, kinds[i], files[i].filename().string().c_str(), options);

        if(res.GetCompilationStatus() != shaderc_compilation_status_success) {
            throw std::runtime_error{ std::format("Could not compile shader: {}, because: \"{}\"", files[i].string(),
                                                  res.GetErrorMessage()) };
        }

        compiled_modules.emplace_back(res.begin(), res.end());

        vks::ShaderModuleCreateInfo module_info;
        module_info.codeSize = compiled_modules.back().size() * sizeof(compiled_modules.back()[0]);
        module_info.pCode = compiled_modules.back().data();
        vkCreateShaderModule(dev, &module_info, nullptr, &shader_modules[types[i]].module);
        shader_modules[types[i]].stage = stages[i];
    }
}

void RendererVulkan::build_rtpp() {
    VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding{};
    accelerationStructureLayoutBinding.binding = 0;
    accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    accelerationStructureLayoutBinding.descriptorCount = 1;
    accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding resultImageLayoutBinding{};
    resultImageLayoutBinding.binding = 1;
    resultImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultImageLayoutBinding.descriptorCount = 1;
    resultImageLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding resultRadianceLayoutBinding{};
    resultRadianceLayoutBinding.binding = 2;
    resultRadianceLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultRadianceLayoutBinding.descriptorCount = 1;
    resultRadianceLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding resultIrradianceLayoutBinding{};
    resultIrradianceLayoutBinding.binding = 3;
    resultIrradianceLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultIrradianceLayoutBinding.descriptorCount = 1;
    resultIrradianceLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding ddgiVisibilityLayoutBinding{};
    ddgiVisibilityLayoutBinding.binding = 4;
    ddgiVisibilityLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ddgiVisibilityLayoutBinding.descriptorCount = 1;
    ddgiVisibilityLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding uniformBufferBinding{};
    uniformBufferBinding.binding = 14;
    uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferBinding.descriptorCount = 1;
    uniformBufferBinding.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding bindlessTexturesBinding{};
    bindlessTexturesBinding.binding = 15;
    bindlessTexturesBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindlessTexturesBinding.descriptorCount = 1024;
    bindlessTexturesBinding.stageFlags = VK_SHADER_STAGE_ALL;

    std::vector<VkDescriptorSetLayoutBinding> bindings({ accelerationStructureLayoutBinding, resultImageLayoutBinding,
                                                         resultRadianceLayoutBinding, resultIrradianceLayoutBinding,
                                                         ddgiVisibilityLayoutBinding, uniformBufferBinding, bindlessTexturesBinding });
    std::vector<VkDescriptorBindingFlags> binding_flags(bindings.size());
    for(uint32_t i = 0; i < binding_flags.size(); ++i) {
        binding_flags.at(i) = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                              VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        if(i == binding_flags.size() - 1) {
            binding_flags.at(i) |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        }
    }
    vks::DescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info;
    binding_flags_info.bindingCount = bindings.size();
    binding_flags_info.pBindingFlags = binding_flags.data();

    VkDescriptorSetLayoutCreateInfo descriptorSetlayoutCI{};
    descriptorSetlayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetlayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptorSetlayoutCI.pBindings = bindings.data();
    descriptorSetlayoutCI.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    descriptorSetlayoutCI.pNext = &binding_flags_info;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &descriptorSetlayoutCI, nullptr, &raytracing_set_layout));

    VkPushConstantRange push_constant_range;
    push_constant_range.stageFlags = VK_SHADER_STAGE_ALL;
    push_constant_range.offset = 0;
    push_constant_range.size = 128;

    VkPipelineLayoutCreateInfo pipelineLayoutCI{};
    pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &raytracing_set_layout;
    pipelineLayoutCI.pushConstantRangeCount = 1;
    pipelineLayoutCI.pPushConstantRanges = &push_constant_range;
    VK_CHECK(vkCreatePipelineLayout(dev, &pipelineLayoutCI, nullptr, &raytracing_layout));

    /*
            Setup ray tracing shader groups
    */
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // Ray generation group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stage.module = shader_modules.at(ShaderModuleType::RT_BASIC_RAYGEN).module;
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Miss group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stage.module = shader_modules.at(ShaderModuleType::RT_BASIC_MISS).module;
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Miss group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stage.module = shader_modules.at(ShaderModuleType::RT_BASIC_MISS2).module;
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    // Closest hit group
    {
        vks::PipelineShaderStageCreateInfo stage;
        stage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stage.module = shader_modules.at(ShaderModuleType::RT_BASIC_CLOSEST_HIT).module;
        stage.pName = "main";
        shaderStages.push_back(stage);
        VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
        shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
        shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        shaderGroups.push_back(shaderGroup);
    }

    /*
            Create the ray tracing pipeline
    */
    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
    rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
    rayTracingPipelineCI.pStages = shaderStages.data();
    rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
    rayTracingPipelineCI.pGroups = shaderGroups.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
    rayTracingPipelineCI.layout = raytracing_layout;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &raytracing_pipeline));
}

void RendererVulkan::build_sbt() {
    const uint32_t handleSize = rt_props.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);
    const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    vkGetRayTracingShaderGroupHandlesKHR(dev, raytracing_pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

    const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    Buffer raygenShaderBindingTable = Buffer{ "buffer_sbt", handleSize * 4, bufferUsageFlags, true };

    // Copy handles
    memcpy(raygenShaderBindingTable.mapped, shaderHandleStorage.data(), handleSize * 4);

    sbt = Buffer{ raygenShaderBindingTable };
}

void RendererVulkan::build_desc_sets() {
    std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 8 },
                                                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 },
                                                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 },
                                                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 },
                                                    { VK_DESCRIPTOR_TYPE_SAMPLER, 1024 } };
    vks::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
    descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    descriptorPoolCreateInfo.poolSizeCount = poolSizes.size();
    descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();
    descriptorPoolCreateInfo.maxSets = 1024;
    VK_CHECK(vkCreateDescriptorPool(dev, &descriptorPoolCreateInfo, nullptr, &raytracing_pool));

    std::vector<uint32_t> variable_counts{ static_cast<uint32_t>(textures.size()) + 8 /*additional space*/ };
    vks::DescriptorSetVariableDescriptorCountAllocateInfo variable_alloc_info;
    variable_alloc_info.descriptorSetCount = variable_counts.size();
    variable_alloc_info.pDescriptorCounts = variable_counts.data();

    vks::DescriptorSetAllocateInfo descriptorSetAllocateInfo;
    descriptorSetAllocateInfo.descriptorPool = raytracing_pool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &raytracing_set_layout;
    descriptorSetAllocateInfo.pNext = &variable_alloc_info;
    VK_CHECK(vkAllocateDescriptorSets(dev, &descriptorSetAllocateInfo, &raytracing_set));

    vks::WriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{};
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures = &tlas;

    vks::WriteDescriptorSet accelerationStructureWrite{};
    // The specialized acceleration structure descriptor has to be chained
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
    accelerationStructureWrite.dstSet = raytracing_set;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo storageImageDescriptor{};
    storageImageDescriptor.imageView = rt_image.view;
    storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo ddgiRadianceDescriptor{};
    ddgiRadianceDescriptor.imageView = ddgi.radiance_texture.view;
    ddgiRadianceDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo ddgiIrradianceDescriptor{};
    ddgiIrradianceDescriptor.imageView = ddgi.irradiance_texture.view;
    ddgiIrradianceDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo ddgiVisibilityDescriptor{};
    ddgiVisibilityDescriptor.imageView = ddgi.visibility_texture.view;
    ddgiVisibilityDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo ubo_descriptor{};
    ubo_descriptor.buffer = ubo.buffer;
    ubo_descriptor.offset = 0;
    ubo_descriptor.range = 1024;

    vks::SamplerCreateInfo sampler_info;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = false;
    sampler_info.compareEnable = false;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.unnormalizedCoordinates = false;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(dev, &sampler_info, nullptr, &sampler));

    std::vector<VkDescriptorImageInfo> bindlessImagesDescriptors;
    for(const auto& tex : textures) {
        bindlessImagesDescriptors.push_back(VkDescriptorImageInfo{
            .sampler = sampler,
            .imageView = tex.view,
            .imageLayout = tex.current_layout,
        });
    }

    bindlessImagesDescriptors.push_back(VkDescriptorImageInfo{
        .sampler = sampler, .imageView = ddgi.radiance_texture.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL });

    bindlessImagesDescriptors.push_back(VkDescriptorImageInfo{
        .sampler = sampler, .imageView = ddgi.irradiance_texture.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL });

    bindlessImagesDescriptors.push_back(VkDescriptorImageInfo{
        .sampler = sampler, .imageView = ddgi.visibility_texture.view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL });

    VkDescriptorImageInfo output_image_infos[]{ storageImageDescriptor };

    vks::WriteDescriptorSet resultImageWrite;
    resultImageWrite.dstSet = raytracing_set;
    resultImageWrite.dstBinding = 1;
    resultImageWrite.dstArrayElement = 0;
    resultImageWrite.descriptorCount = 1;
    resultImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultImageWrite.pImageInfo = output_image_infos;

    vks::WriteDescriptorSet ddgiRadianceImageWrite;
    ddgiRadianceImageWrite.dstSet = raytracing_set;
    ddgiRadianceImageWrite.dstBinding = 2;
    ddgiRadianceImageWrite.dstArrayElement = 0;
    ddgiRadianceImageWrite.descriptorCount = 1;
    ddgiRadianceImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ddgiRadianceImageWrite.pImageInfo = &ddgiRadianceDescriptor;

    vks::WriteDescriptorSet ddgiIrradianceWrite;
    ddgiIrradianceWrite.dstSet = raytracing_set;
    ddgiIrradianceWrite.dstBinding = 3;
    ddgiIrradianceWrite.dstArrayElement = 0;
    ddgiIrradianceWrite.descriptorCount = 1;
    ddgiIrradianceWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ddgiIrradianceWrite.pImageInfo = &ddgiIrradianceDescriptor;

    vks::WriteDescriptorSet ddgiVisibilityWrite;
    ddgiVisibilityWrite.dstSet = raytracing_set;
    ddgiVisibilityWrite.dstBinding = 4;
    ddgiVisibilityWrite.dstArrayElement = 0;
    ddgiVisibilityWrite.descriptorCount = 1;
    ddgiVisibilityWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    ddgiVisibilityWrite.pImageInfo = &ddgiVisibilityDescriptor;

    vks::WriteDescriptorSet uniformBufferWrite;
    uniformBufferWrite.dstSet = raytracing_set;
    uniformBufferWrite.dstBinding = 14;
    uniformBufferWrite.dstArrayElement = 0;
    uniformBufferWrite.descriptorCount = 1;
    uniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformBufferWrite.pBufferInfo = &ubo_descriptor;

    vks::WriteDescriptorSet bindlessTexturesWrite;
    bindlessTexturesWrite.dstSet = raytracing_set;
    bindlessTexturesWrite.dstBinding = 15;
    bindlessTexturesWrite.dstArrayElement = 0;
    bindlessTexturesWrite.descriptorCount = bindlessImagesDescriptors.size();
    bindlessTexturesWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindlessTexturesWrite.pImageInfo = bindlessImagesDescriptors.data();

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = { accelerationStructureWrite, resultImageWrite,
                                                              ddgiRadianceImageWrite,     ddgiIrradianceWrite,
                                                              ddgiVisibilityWrite,           uniformBufferWrite,
                                                              bindlessTexturesWrite };
    vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
}

void RendererVulkan::create_rt_output_image() {
    const auto* window = Engine::window();
    rt_image = Image{
        "rt_image", window->size[0], window->size[1], 1, 1, 1, swapchain_format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    };

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    rt_image.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                               VK_ACCESS_2_SHADER_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    submit_recording(gq, cmd);
    vkQueueWaitIdle(gq);
}

void RendererVulkan::build_blas() {
    std::vector<uint32_t> dirty;
    for(uint32_t i = 0; i < models.size(); ++i) {
        if(models.at(i).flags & VkRendererFlags::DIRTY_BLAS) {
            // TODO: Maybe move it to the end, when the building is finished
            dirty.push_back(i);
            models.at(i).flags ^= VkRendererFlags::DIRTY_BLAS;
        }
    }

    std::vector<vks::AccelerationStructureGeometryTrianglesDataKHR> triangles(dirty.size());
    std::vector<vks::AccelerationStructureGeometryKHR> geometries(dirty.size());
    std::vector<vks::AccelerationStructureBuildGeometryInfoKHR> build_geometries(dirty.size());
    std::vector<uint32_t> scratch_sizes(dirty.size());
    std::vector<vks::AccelerationStructureBuildRangeInfoKHR> offsets(dirty.size());
    Buffer scratch_buffer;

    for(uint32_t i = 0; i < dirty.size(); ++i) {
        const RenderModel& model = models.at(dirty.at(i));
        RenderModelRTMetadata& meta = rt_metadata.at(dirty.at(i));

        triangles.at(i).vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.at(i).vertexData.deviceAddress = vertex_buffer.bda;
        triangles.at(i).vertexStride = sizeof(Vertex);
        triangles.at(i).indexType = VK_INDEX_TYPE_UINT32;
        triangles.at(i).indexData.deviceAddress = index_buffer.bda;
        triangles.at(i).maxVertex = model.first_vertex + model.vertex_count - 1u;
        triangles.at(i).transformData = {};

        geometries.at(i).geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometries.at(i).flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometries.at(i).geometry.triangles = triangles.at(i);

        build_geometries.at(i).type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_geometries.at(i).flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_geometries.at(i).geometryCount = 1;
        build_geometries.at(i).pGeometries = &geometries.at(i);
        build_geometries.at(i).mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        const uint32_t max_primitives = model.index_count / 3u;
        vks::AccelerationStructureBuildSizesInfoKHR build_size_info;
        vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &build_geometries.at(i), &max_primitives, &build_size_info);

        meta.blas_buffer =
            Buffer{ "blas_buffer", build_size_info.accelerationStructureSize,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
        scratch_sizes.at(i) = build_size_info.buildScratchSize;

        vks::AccelerationStructureCreateInfoKHR blas_info;
        blas_info.buffer = meta.blas_buffer.buffer;
        blas_info.size = build_size_info.accelerationStructureSize;
        blas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VK_CHECK(vkCreateAccelerationStructureKHR(dev, &blas_info, nullptr, &meta.blas));
    }

    const auto total_scratch_size = std::accumulate(scratch_sizes.begin(), scratch_sizes.end(), 0ul);
    scratch_buffer = Buffer{ "blas_scratch_buffer", total_scratch_size, rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    for(uint32_t i = 0, offset = 0; i < dirty.size(); ++i) {
        const RenderModel& model = models.at(dirty.at(i));
        RenderModelRTMetadata& meta = rt_metadata.at(dirty.at(i));

        build_geometries.at(i).scratchData.deviceAddress = scratch_buffer.bda + offset;
        build_geometries.at(i).dstAccelerationStructure = meta.blas;

        const uint32_t max_primitives = model.index_count / 3u;
        offsets.at(i).firstVertex = 0;
        offsets.at(i).primitiveCount = max_primitives;
        offsets.at(i).primitiveOffset = (model.first_index) * sizeof(model.first_index);
        offsets.at(i).transformOffset = 0;

        offset += scratch_sizes.at(i);
    }

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> poffsets(dirty.size());
    for(uint32_t i = 0; i < dirty.size(); ++i) {
        poffsets.at(i) = &offsets.at(i);
    }
    vkCmdBuildAccelerationStructuresKHR(cmd, build_geometries.size(), build_geometries.data(), poffsets.data());
    submit_recording(gq, cmd);
    vkDeviceWaitIdle(dev);
}

void RendererVulkan::build_tlas() {
    std::vector<const RenderModelInstance*> render_model_instances;
    render_model_instances.reserve(model_instances.size());

    for(auto& i : model_instances) {
        if(i.flags & InstanceFlags::RAY_TRACED_BIT) { render_model_instances.push_back(&i); }
    }

    std::vector<vks::AccelerationStructureInstanceKHR> instances(render_model_instances.size());
    for(uint32_t i = 0; i < instances.size(); ++i) {
        auto& instance = instances.at(i);
        instance.transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(render_model_instances.at(i)->transform));
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = rt_metadata.at(*render_model_instances.at(i)->model).blas_buffer.bda;
    }

    Buffer buffer_instance{ "tlas_instance_buffer", sizeof(instances[0]) * instances.size(),
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                            true };
    memcpy(buffer_instance.mapped, instances.data(), sizeof(instances[0]) * instances.size());

    vks::AccelerationStructureGeometryKHR geometry;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = false;
    geometry.geometry.instances.data.deviceAddress = buffer_instance.bda;

    vks::AccelerationStructureBuildGeometryInfoKHR tlas_info;
    tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlas_info.geometryCount = 1; // must be 1 for TLAS
    tlas_info.pGeometries = &geometry;
    tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    vks::AccelerationStructureBuildSizesInfoKHR build_size;
    const uint32_t max_primitives = instances.size();
    vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_info,
                                            &max_primitives, &build_size);

    Buffer buffer_tlas{ "tlas_buffer", build_size.accelerationStructureSize,
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false };

    vks::AccelerationStructureCreateInfoKHR acc_info;
    acc_info.buffer = buffer_tlas.buffer;
    acc_info.size = build_size.accelerationStructureSize;
    acc_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(dev, &acc_info, nullptr, &tlas);

    Buffer buffer_scratch{ "tlas_scratch_buffer", build_size.buildScratchSize, rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    tlas_info.dstAccelerationStructure = tlas;
    tlas_info.scratchData.deviceAddress = buffer_scratch.bda;

    vks::AccelerationStructureBuildRangeInfoKHR build_range;
    build_range.primitiveCount = max_primitives;
    build_range.primitiveOffset = 0;
    build_range.firstVertex = 0;
    build_range.transformOffset = 0;
    VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
    submit_recording(gq, cmd);
    vkDeviceWaitIdle(dev);

    tlas_buffer = Buffer{ buffer_tlas };
}

void RendererVulkan::prepare_ddgi() {
#if 1
    for(auto& mi : model_instances) {
        auto& m = models.at(*mi.model);

        for(uint32_t i = 0; i < m.vertex_count; ++i) {
            scene_bounding_box.min =
                glm::min(scene_bounding_box.min, ((Vertex*)vertex_buffer.mapped + m.first_vertex + i)->pos);
            scene_bounding_box.max =
                glm::max(scene_bounding_box.max, ((Vertex*)vertex_buffer.mapped + m.first_vertex + i)->pos);
        }
    }

    ddgi.probe_dims = scene_bounding_box;
    ddgi.probe_dims.min *= glm::vec3{ 0.9f, 0.7f, 0.9f };
    ddgi.probe_dims.max *= glm::vec3{ 0.9f, 0.7f, 0.9f };

    ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
    ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
                          std::bit_ceil(ddgi.probe_counts.z) };

    ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };

    const uint32_t irradiance_texture_width = (ddgi.irradiance_resolution + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const uint32_t irradiance_texture_height = (ddgi.irradiance_resolution + 2) * ddgi.probe_counts.z;
    const uint32_t visibility_texture_width = (ddgi.visibility_resolution + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const uint32_t visibility_texture_height = (ddgi.visibility_resolution + 2) * ddgi.probe_counts.z;

    ddgi.radiance_texture = Image{ "ddgi radiance",
                                   ddgi.rays_per_probe,
                                   ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z,
                                   1,
                                   1,
                                   1,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_SAMPLE_COUNT_1_BIT,
                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
    ddgi.irradiance_texture = Image{
        "ddgi irradiance", irradiance_texture_width, irradiance_texture_height, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    };
    ddgi.visibility_texture = Image{
        "ddgi visibility", visibility_texture_width, visibility_texture_height, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    };

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    ddgi.radiance_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.irradiance_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                              VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.visibility_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                              VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    submit_recording(gq, cmd);

    vks::SamplerCreateInfo sampler_info;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.anisotropyEnable = false;
    sampler_info.compareEnable = false;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.unnormalizedCoordinates = false;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(dev, &sampler_info, nullptr, &sampler));

    ddgi_buffer = Buffer{ "ddgi_settings_buffer", sizeof(DDGI_Buffer),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };
    DDGI_Buffer* ddgi_buffer_mapped = static_cast<DDGI_Buffer*>(ddgi_buffer.mapped);
    ddgi_buffer_mapped->probe_start = ddgi.probe_dims.min;
    ddgi_buffer_mapped->probe_counts = ddgi.probe_counts;
    ddgi_buffer_mapped->probe_walk = ddgi.probe_walk;
    ddgi_buffer_mapped->min_dist = 0.08;
    ddgi_buffer_mapped->max_dist = ddgi.probe_dims.size().length() * 1.1f;
    ddgi_buffer_mapped->normal_bias = 0.25f;
    ddgi_buffer_mapped->irradiance_resolution = ddgi.irradiance_resolution;
    ddgi_buffer_mapped->visibility_resolution = ddgi.visibility_resolution;
    ddgi_buffer_mapped->rays_per_probe = ddgi.rays_per_probe;
    ddgi_buffer_mapped->radiance_tex_idx = textures.size();

    vks::ComputePipelineCreateInfo compute_info;
    compute_info.layout = raytracing_layout;
    compute_info.stage = vks::PipelineShaderStageCreateInfo{};
    compute_info.stage.pName = "main";
    compute_info.stage.module = shader_modules.at(ShaderModuleType::RT_BASIC_PROBE_IRRADIANCE_COMPUTE).module;
    compute_info.stage.stage = shader_modules.at(ShaderModuleType::RT_BASIC_PROBE_IRRADIANCE_COMPUTE).stage;
    VK_CHECK(vkCreateComputePipelines(dev, nullptr, 1, &compute_info, nullptr, &ddgi_compute_pipeline));

    vkQueueWaitIdle(gq);
#endif
}

VkCommandBuffer RendererVulkan::begin_recording(VkCommandPool pool, VkCommandBufferUsageFlags usage) {
    VkCommandBuffer cmd = get_or_allocate_free_command_buffer(pool);

    vks::CommandBufferBeginInfo info;
    info.flags = usage;
    VK_CHECK(vkBeginCommandBuffer(cmd, &info));

    return cmd;
}

void RendererVulkan::submit_recording(VkQueue queue, VkCommandBuffer buffer,
                                      const std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>>& wait_sems,
                                      const std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>>& signal_sems, VkFence fence) {
    VK_CHECK(vkEndCommandBuffer(buffer));

    vks::CommandBufferSubmitInfo buffer_info;
    buffer_info.commandBuffer = buffer;

    std::vector<vks::SemaphoreSubmitInfo> waits(wait_sems.size());
    for(uint32_t i = 0; i < wait_sems.size(); ++i) {
        waits.at(i).semaphore = wait_sems.at(i).first;
        waits.at(i).stageMask = wait_sems.at(i).second;
    }

    std::vector<vks::SemaphoreSubmitInfo> signals(signal_sems.size());
    for(uint32_t i = 0; i < signal_sems.size(); ++i) {
        signals.at(i).semaphore = signal_sems.at(i).first;
        signals.at(i).stageMask = signal_sems.at(i).second;
    }

    vks::SubmitInfo2 submit_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &buffer_info;
    submit_info.waitSemaphoreInfoCount = waits.size();
    submit_info.pWaitSemaphoreInfos = waits.data();
    submit_info.signalSemaphoreInfoCount = signals.size();
    submit_info.pSignalSemaphoreInfos = signals.data();

    VK_CHECK(vkQueueSubmit2(queue, 1, &submit_info, fence));
}

void RendererVulkan::submit_recordings(VkQueue queue, const std::vector<RecordingSubmitInfo>& submits, VkFence fence) {
    std::vector<vks::SubmitInfo2> infos(submits.size());
    std::vector<std::vector<vks::CommandBufferSubmitInfo>> buffer_submits(submits.size());
    std::vector<std::vector<vks::SemaphoreSubmitInfo>> wait_submits(submits.size());
    std::vector<std::vector<vks::SemaphoreSubmitInfo>> signal_submits(submits.size());

    for(uint32_t i = 0; i < submits.size(); ++i) {
        const auto& buffers = submits.at(i).buffers;
        const auto& waits = submits.at(i).waits;
        const auto& signals = submits.at(i).signals;

        buffer_submits.at(i).resize(buffers.size());
        for(uint32_t j = 0; j < buffers.size(); ++j) {
            buffer_submits.at(i).at(j).commandBuffer = buffers.at(j);
        }

        wait_submits.at(i).resize(waits.size());
        for(uint32_t j = 0; j < waits.size(); ++j) {
            wait_submits.at(i).at(j).semaphore = waits.at(j).first;
            wait_submits.at(i).at(j).stageMask = waits.at(j).second;
        }

        signal_submits.at(i).resize(signals.size());
        for(uint32_t j = 0; j < signals.size(); ++j) {
            signal_submits.at(i).at(j).semaphore = signals.at(j).first;
            signal_submits.at(i).at(j).stageMask = signals.at(j).second;
        }

        infos.at(i).commandBufferInfoCount = buffer_submits.at(i).size();
        infos.at(i).pCommandBufferInfos = buffer_submits.at(i).data();
        infos.at(i).waitSemaphoreInfoCount = wait_submits.at(i).size();
        infos.at(i).pWaitSemaphoreInfos = wait_submits.at(i).data();
        infos.at(i).signalSemaphoreInfoCount = signal_submits.at(i).size();
        infos.at(i).pSignalSemaphoreInfos = signal_submits.at(i).data();
    }

    VK_CHECK(vkQueueSubmit2(queue, infos.size(), infos.data(), fence));
}

void RendererVulkan::end_recording(VkCommandBuffer buffer) { VK_CHECK(vkEndCommandBuffer(buffer)); }

void RendererVulkan::reset_command_pool(VkCommandPool pool) {
    VK_CHECK(vkResetCommandPool(dev, pool, {}));
    free_pool_buffers[pool] = std::move(used_pool_buffers[pool]);
}

VkCommandBuffer RendererVulkan::get_or_allocate_free_command_buffer(VkCommandPool pool) {
    if(free_pool_buffers[pool].empty()) {
        vks::CommandBufferAllocateInfo info;
        info.commandPool = pool;
        info.commandBufferCount = 1;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        auto& buffer = used_pool_buffers[pool].emplace_back(nullptr);
        VK_CHECK(vkAllocateCommandBuffers(dev, &info, &buffer));
        return buffer;
    } else {
        used_pool_buffers[pool].push_back(free_pool_buffers[pool].back());
        free_pool_buffers[pool].erase(free_pool_buffers[pool].end() - 1);
        return used_pool_buffers[pool].back();
    }
}

Image::Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers,
             VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage)
    : format(format), mips(mips), layers(layers), width(width), height(height), depth(depth) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::ImageCreateInfo iinfo;

    int dims = -1;
    if(width > 1) { ++dims; }
    if(height > 1) { ++dims; }
    if(depth > 1) { ++dims; }
    if(dims == -1) { dims = 1; }
    VkImageType types[]{ VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };

    iinfo.imageType = types[dims];
    iinfo.format = format;
    iinfo.extent = { width, height, depth };
    iinfo.mipLevels = mips;
    iinfo.arrayLayers = layers;
    iinfo.samples = samples;
    iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    iinfo.usage = usage;
    iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo vmainfo{};
    vmainfo.usage = VMA_MEMORY_USAGE_AUTO;

    VK_CHECK(vmaCreateImage(renderer->vma, &iinfo, &vmainfo, &image, &alloc, nullptr));

    VkImageViewType view_types[]{ VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D };

    VkImageAspectFlags view_aspect{ VK_IMAGE_ASPECT_COLOR_BIT };
    if(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    vks::ImageViewCreateInfo ivinfo;
    ivinfo.image = image;
    ivinfo.viewType = view_types[dims];
    ivinfo.components = {};
    ivinfo.format = format;
    ivinfo.subresourceRange = { .aspectMask = view_aspect, .baseMipLevel = 0, .levelCount = mips, .baseArrayLayer = 0, .layerCount = 1 };

    VK_CHECK(vkCreateImageView(renderer->dev, &ivinfo, nullptr, &view));

    set_debug_name(image, name);
    set_debug_name(view, std::format("{}_default_view", name));
}

void Image::transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                              VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, bool from_undefined,
                              VkImageLayout dst_layout) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::ImageMemoryBarrier2 imgb;
    imgb.image = image;
    imgb.oldLayout = from_undefined ? VK_IMAGE_LAYOUT_UNDEFINED : current_layout;
    imgb.newLayout = dst_layout;
    imgb.srcStageMask = src_stage;
    imgb.srcAccessMask = src_access;
    imgb.dstStageMask = dst_stage;
    imgb.dstAccessMask = dst_access;
    imgb.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = mips,
        .baseArrayLayer = 0,
        .layerCount = layers,
    };

    vks::DependencyInfo dep;
    dep.pImageMemoryBarriers = &imgb;
    dep.imageMemoryBarrierCount = 1;
    vkCmdPipelineBarrier2(cmd, &dep);

    current_layout = dst_layout;
}
