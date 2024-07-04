#include <volk/volk.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"

Buffer::Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map) {
    RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
    vks::BufferCreateInfo binfo;
    VmaAllocationCreateInfo vinfo{};
    const auto use_bda = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) > 0;

    binfo.size = size;
    binfo.usage = usage;

    vinfo.usage = VMA_MEMORY_USAGE_AUTO;
    if(map) { vinfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT; }
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

    ENG_WARN("ALLOCATING BUFFER %s OF SIZE %.2f KB", name.c_str(), static_cast<float>(size) / 1024.0f);
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

    std::memcpy(mapped, data.data(), size_to_copy);

    size += size_to_copy;

    return size_to_copy;
}

// Image::Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers, VkFormat format,
//              VkSampleCountFlagBits samples, VkImageUsageFlags usage)
//     : format(format), mips(mips), layers(layers) {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     vks::ImageCreateInfo iinfo;
//
//     int dims = -1;
//     if(width > 1) { ++dims; }
//     if(height > 1) { ++dims; }
//     if(depth > 1) { ++dims; }
//     VkImageType types[]{ VK_IMAGE_TYPE_1D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };
//
//     iinfo.imageType = types[dims];
//     iinfo.format = format;
//     iinfo.extent = { width, height, depth };
//     iinfo.mipLevels = mips;
//     iinfo.arrayLayers = layers;
//     iinfo.samples = samples;
//     iinfo.tiling = VK_IMAGE_TILING_OPTIMAL;
//     iinfo.usage = usage;
//     iinfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//     iinfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//
//     VmaAllocationCreateInfo vmainfo{};
//     vmainfo.usage = VMA_MEMORY_USAGE_AUTO;
//
//     if(vmaCreateImage(vma, &iinfo, &vmainfo, &image, &alloc, nullptr) != VK_SUCCESS) {
//         throw std::runtime_error{ "Could not create image" };
//     }
//
//     VkImageViewType view_types[]{ VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D };
//
//     VkImageAspectFlags view_aspect{ VK_IMAGE_ASPECT_COLOR_BIT };
//     if(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) { view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT; }
//
//     vks::ImageViewCreateInfo ivinfo;
//     ivinfo.image = image;
//     ivinfo.viewType = view_types[dims];
//     ivinfo.components = {};
//     ivinfo.format = format;
//     ivinfo.subresourceRange = { .aspectMask = view_aspect, .baseMipLevel = 0, .levelCount = mips, .baseArrayLayer = 0, .layerCount = 1 };
//
//     if(vkCreateImageView(dev, &ivinfo, nullptr, &view) != VK_SUCCESS) {
//         throw std::runtime_error{ "Could not create image default view" };
//     }
//
//     set_debug_name(image, name);
//     set_debug_name(view, std::format("{}_default_view", name));
// }
//
// void Image::transition_layout(VkImageLayout dst, bool from_undefined) {
//     RendererVulkan* renderer = static_cast<RendererVulkan*>(Engine::renderer());
//     vks::ImageMemoryBarrier2 imgb;
//     imgb.image = image;
//     imgb.oldLayout = from_undefined ? VK_IMAGE_LAYOUT_UNDEFINED : current_layout;
//     imgb.newLayout = dst;
//     imgb.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
//     imgb.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
//     imgb.srcAccessMask = VK_ACCESS_NONE;
//     imgb.dstAccessMask = VK_ACCESS_NONE;
//     imgb.subresourceRange = {
//         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
//         .baseMipLevel = 0,
//         .levelCount = mips,
//         .baseArrayLayer = 0,
//         .layerCount = layers,
//     };
//
//     vks::CommandBufferBeginInfo cmdbi;
//     cmdbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//     vkBeginCommandBuffer(cmd, &cmdbi);
//
//     vks::DependencyInfo dep;
//     dep.pImageMemoryBarriers = &imgb;
//     dep.imageMemoryBarrierCount = 1;
//     vkCmdPipelineBarrier2(cmd, &dep);
//
//     vkEndCommandBuffer(cmd);
//
//     vks::CommandBufferSubmitInfo cmdsinfo;
//     cmdsinfo.commandBuffer = cmd;
//
//     vks::SubmitInfo2 sinfo;
//     sinfo.commandBufferInfoCount = 1;
//     sinfo.pCommandBufferInfos = &cmdsinfo;
//     vkQueueSubmit2(gq, 1, &sinfo, nullptr);
//     vkQueueWaitIdle(gq);
//
//     current_layout = dst;
// }

void RendererVulkan::init() {
    initialize_vulkan();
    create_swapchain();
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

    vks::PhysicalDeviceRayTracingPipelineFeaturesKHR rtpp_features;
    rtpp_features.rayTracingPipeline = true;
    rtpp_features.rayTraversalPrimitiveCulling = true;

    auto dev_ret = device_builder.add_pNext(&dev_2_features)
                       .add_pNext(&desc_features)
                       .add_pNext(&dyn_features)
                       .add_pNext(&query_reset_features)
                       .add_pNext(&synch2_features)
                       .add_pNext(&bda_features)
                       .add_pNext(&acc_features)
                       .add_pNext(&rtpp_features)
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
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR pdev_rtpp_props{};
    pdev_rtpp_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    pdev_props.pNext = &pdev_rtpp_props;
    vkGetPhysicalDeviceProperties2(phys_ret->physical_device, &pdev_props);

    instance = vkb_inst.instance;
    dev = device;
    pdev = phys_ret->physical_device;
    gqi = vkb_device.get_queue_index(vkb::QueueType::graphics).value();
    gq = vkb_device.get_queue(vkb::QueueType::graphics).value();
    pqi = vkb_device.get_queue_index(vkb::QueueType::present).value();
    pq = vkb_device.get_queue(vkb::QueueType::present).value();
    rt_props = pdev_rtpp_props;

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
    if(vmaCreateAllocator(&allocatorCreateInfo, &allocator) != VK_SUCCESS) { throw std::runtime_error{ "Could not create vma" }; }

    vma = allocator;

    vks::CommandPoolCreateInfo cmdpi;
    cmdpi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdpi.queueFamilyIndex = gqi;

    VkCommandPool pool;
    if(vkCreateCommandPool(device, &cmdpi, nullptr, &pool) != VK_SUCCESS) { throw std::runtime_error{ "Could not create command pool" }; }

    vks::CommandBufferAllocateInfo cmdi;
    cmdi.commandPool = pool;
    cmdi.commandBufferCount = 1;
    cmdi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VkCommandBuffer cmd;
    if(vkAllocateCommandBuffers(device, &cmdi, &cmd) != VK_SUCCESS) { throw std::runtime_error{ "Could not allocate command buffer" }; }

    cmdpool = pool;
    cmd = cmd;
    // ubo = Buffer{ "ubo", 128, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true };
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
    vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_swapchain_image);
    vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_tracing_done);
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

    VkSwapchainKHR swapchain;
    if(const auto error = vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain); error != VK_SUCCESS) {
        throw std::runtime_error{ "Could not create swapchain" };
    }

    uint32_t num_images;
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, nullptr);

    std::vector<VkImage> images(num_images);
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, images.data());

    swapchain = swapchain;
    swapchain_images = images;
    swapchain_format = sinfo.imageFormat;
}

void RendererVulkan::batch_model(ImportedModel& model, Flags<ModelBatchFlags> flags) {
    for(const auto& tex : model.textures) {
        ENG_WARN("REMEMBER TO LOAD TEXTURES");
    }

    for(const auto& mat : model.materials) {
        ENG_WARN("REMEMBER TO LOAD MATERIALS");
    }

    std::vector<Vertex> vertices(model.vertices.size());
    std::vector<uint32_t> indices(model.indices.size());

    for(size_t i = 0; i < model.vertices.size(); ++i) {
        const auto& v = model.vertices.at(i);
        vertices.at(i) = Vertex{
            .pos = v.pos,
            .nor = v.nor,
            .uv = v.uv,
        };
    }

    for(size_t i = 0, offset = 0; i < model.meshes.size(); ++i) {
        const auto& m = model.meshes.at(i);
        for(size_t j = 0; j < m.index_count; ++j) {
            indices.at(j + offset) = model.indices.at(j + m.index_offset) + m.vertex_offset;
        }
        offset += m.index_count;
    }

    vertex_buffer.push_data(std::as_bytes(std::span{ vertices }));
    index_buffer.push_data(std::as_bytes(std::span{ indices }));
}

//Handle<A> RendererVulkan::build_blas() { }
