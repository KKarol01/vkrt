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

Buffer::Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map)
    : Buffer(name, size, 1u, usage, map) {}

Buffer::Buffer(const std::string& name, size_t size, u32 alignment, VkBufferUsageFlags usage, bool map)
    : Buffer(name, VkBufferCreateInfo{ .size = size, .usage = usage },
             VmaAllocationCreateInfo{
                 .flags = (map ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u),
                 .usage = VMA_MEMORY_USAGE_AUTO,
             },
             alignment) {}

Buffer::Buffer(const std::string& name, vks::BufferCreateInfo create_info, VmaAllocationCreateInfo alloc_info, u32 alignment)
    : name{ name }, capacity{ create_info.size }, alignment{ alignment } {
    u32 queue_family_indices[]{ get_renderer().gqi, get_renderer().tqi1 };
    if(queue_family_indices[0] != queue_family_indices[1]) {
        create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    }
    if(!(alloc_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        create_info.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if(create_info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        alloc_info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    }

    create_info.size = std::max(create_info.size, 1ull);

    VmaAllocationInfo vainfo{};
    if(alignment > 1) {
        VK_CHECK(vmaCreateBufferWithAlignment(get_renderer().vma, &create_info, &alloc_info, alignment, &buffer, &alloc, &vainfo));
    } else {
        VK_CHECK(vmaCreateBuffer(get_renderer().vma, &create_info, &alloc_info, &buffer, &alloc, &vainfo));
    }

    if(vainfo.pMappedData) { mapped = vainfo.pMappedData; };

    if(create_info.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        vks::BufferDeviceAddressInfo bdainfo;
        bdainfo.buffer = buffer;
        bda = vkGetBufferDeviceAddress(get_renderer().dev, &bdainfo);
    }

    usage = create_info.usage;

    set_debug_name(buffer, name);
    ENG_LOG("ALLOCATING BUFFER {} OF SIZE {:.2f} KB", name.c_str(), static_cast<float>(capacity) / 1024.0f);
}

Buffer::Buffer(Buffer&& other) noexcept { *this = std::move(other); }

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    vmaDestroyBuffer(get_renderer().vma, buffer, alloc);

    name = std::move(other.name);
    usage = other.usage;
    size = other.size;
    capacity = other.capacity;
    alignment = other.alignment;
    buffer = std::exchange(other.buffer, nullptr);
    alloc = std::exchange(other.alloc, nullptr);
    mapped = std::exchange(other.mapped, nullptr);
    bda = std::exchange(other.bda, VkDeviceAddress{});

    return *this;
}

bool Buffer::push_data(std::span<const std::byte> data, u32 offset) {
    if(!buffer) {
        assert(false && "Buffer was not created correctly");
        return false;
    }

    if(offset > capacity) {
        ENG_WARN("Provided buffer offset is bigger than the capacity");
        return false;
    }

    const auto size_after = offset + data.size_bytes();

    if(size_after > capacity) {
        size_t new_size = std::ceill(static_cast<long double>(capacity) * 1.5l);
        if(new_size < size_after) { new_size = size_after; }
        ENG_LOG("Resizing buffer {}", name);
        if(!resize(new_size)) {
            ENG_LOG("Failed to resize the buffer {}", name);
            return false;
        }
    }

    if(mapped) {
        memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size_bytes());
    } else {
        std::atomic_flag flag{};
        if(!get_renderer().staging->send_to(GpuStagingUpload{ .dst = buffer, .src = data, .dst_offset = offset, .size_bytes = data.size_bytes() },
                                            {}, {}, &flag)) {
            return false;
        }
        flag.wait(false);
    }

    size = std::max(size, offset + data.size_bytes());

    return true;
}

bool Buffer::resize(size_t new_size) {
    Buffer new_buffer{ name, new_size, alignment, usage, !!mapped };

    bool success = false;
    std::atomic_flag flag{};
    if(mapped) {
        success = new_buffer.push_data(std::span{ static_cast<const std::byte*>(mapped), size });
        flag.test_and_set();
    } else {
        success = get_renderer().staging->send_to(GpuStagingUpload{ .dst = new_buffer.buffer, .src = buffer, .size_bytes = size },
                                                  {}, {}, &flag);
    }

    if(!success) { return false; }
    flag.wait(false);

    *this = std::move(new_buffer);
    return true;
}

void Buffer::deallocate() {
    if(buffer && alloc) { vmaDestroyBuffer(get_renderer().vma, buffer, alloc); }
}

void RendererVulkan::init() {
    initialize_vulkan();
    create_rt_output_image();
    compile_shaders();
    build_pipelines();
    build_sbt();
    initialize_imgui();
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
        // const auto proj = glm::perspective(190.0f, (float)1024.0f / (float)768.0f, 0.0f, 10.0f);
        // Engine::camera()->update_projection(proj);
    }
}

void RendererVulkan::initialize_vulkan() {
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

    VmaAllocator allocator;
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &allocator));
    vma = allocator;

    staging = std::make_unique<GpuStagingManager>(tq1, tqi1, 1024 * 1024 * 64); // 64MB

    global_buffer = Buffer{ "globals", 512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    vertex_buffer = Buffer{ "vertex_buffer", 0ull,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            false };
    index_buffer = Buffer{ "index_buffer", 0ull,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           false };

    for(int i = 0; i < sizeof(primitives) / sizeof(primitives[0]); ++i) {
        RenderingPrimitives& primitives = this->primitives[i];
        vks::SemaphoreCreateInfo sem_swapchain_info;
        vks::FenceCreateInfo fence_info{ VkFenceCreateInfo{ .flags = VK_FENCE_CREATE_SIGNALED_BIT } };
        VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_swapchain_image));
        VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_rendering_finished));
        VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_gui_start));
        VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_copy_to_sw_img_done));
        VK_CHECK(vkCreateFence(dev, &fence_info, nullptr, &primitives.fen_rendering_finished));
        primitives.cmdpool = CommandPool{ gqi, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT };
        mesh_instance_transform_buffers[i] =
            new Buffer{ "mesh instance transforms", 0ull,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    }
}

void RendererVulkan::create_swapchain() {
    if(swapchain) { vkDestroySwapchainKHR(dev, swapchain, nullptr); }

    VkFormat view_formats[]{
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
    };

    VkImageFormatListCreateInfo format_list_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2,
        .pViewFormats = view_formats,
    };

    vks::SwapchainCreateInfoKHR sinfo{ VkSwapchainCreateInfoKHR{
        .pNext = &format_list_info,
        .flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR,
        .surface = window_surface,
        .minImageCount = 2,
        .imageFormat = VK_FORMAT_R8G8B8A8_SRGB,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = VkExtent2D{ Engine::window()->width, Engine::window()->height },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = true,
    } };

    VK_CHECK(vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain));
    u32 num_images;
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, nullptr);

    std::vector<VkImage> images(num_images);
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, images.data());

    swapchain_format = sinfo.imageFormat;

    for(int i = 0; i < 2; ++i) {
        if(imgui_views[i]) { vkDestroyImageView(dev, imgui_views[i], nullptr); }
        vkDestroyImageView(dev, std::exchange(swapchain_images[i].view, nullptr), nullptr);
        swapchain_images[i].image = nullptr;

        swapchain_images[i] = Image{ std::format("swapchain_image_{}", i),
                                     images.at(i),
                                     sinfo.imageExtent.width,
                                     sinfo.imageExtent.height,
                                     1u,
                                     1u,
                                     sinfo.imageArrayLayers,
                                     sinfo.imageFormat,
                                     VK_SAMPLE_COUNT_1_BIT,
                                     sinfo.imageUsage };

        vks::ImageViewCreateInfo imgui_image_view;
        imgui_image_view.format = VK_FORMAT_R8G8B8A8_UNORM;
        imgui_image_view.image = images[i];
        imgui_image_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imgui_image_view.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        };
        VK_CHECK(vkCreateImageView(dev, &imgui_image_view, nullptr, &imgui_views[i]));
        set_debug_name(images[i], std::format("swapchain_image_{}", i));
        set_debug_name(imgui_views[i], std::format("imgui_image_view_{}", i));
    }
}

void RendererVulkan::initialize_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Engine::window()->window, true);

    VkFormat color_formats[]{ VK_FORMAT_R8G8B8A8_SRGB };

    ImGui_ImplVulkan_InitInfo init_info = { 
        .Instance = instance,
        .PhysicalDevice = pdev,
        .Device = dev,
        .QueueFamily = gqi,
        .Queue = gq,
        .DescriptorPool = imgui_desc_pool,
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

    auto cmdimgui = get_primitives().cmdpool.begin_onetime();
    ImGui_ImplVulkan_CreateFontsTexture();
    get_primitives().cmdpool.end(cmdimgui);
    scheduler_gq.enqueue_wait_submit({ { cmdimgui } });
    vkQueueWaitIdle(gq);
}

void RendererVulkan::render() {
    if(Engine::window()->width == 0 || Engine::window()->height == 0) { return; }
    if(flags.test_clear(RendererFlags::DIRTY_GEOMETRY_BATCHES_BIT)) { upload_staged_models(); }
    if(flags.test_clear(RendererFlags::DIRTY_MESH_INSTANCES)) { upload_instances(); }
    if(flags.test_clear(RendererFlags::DIRTY_MESH_BLAS_BIT)) { build_blas(); }
    if(flags.test_clear(RendererFlags::DIRTY_TLAS_BIT)) {
        build_tlas();
        if(Engine::frame_num() < 100) { prepare_ddgi(); }
    }
    if(flags.test_clear(RendererFlags::REFIT_TLAS_BIT)) { refit_tlas(); }
    if(flags.test_clear(RendererFlags::UPLOAD_MESH_INSTANCE_TRANSFORMS_BIT)) { upload_transforms(); }
    if(flags.test_clear(RendererFlags::RESIZE_SWAPCHAIN_BIT)) {
        vkQueueWaitIdle(gq);
        create_swapchain();
    }
    if(flags.test_clear(RendererFlags::RESIZE_SCREEN_RECT_BIT)) {
        vkQueueWaitIdle(gq);
        for(int i = 0; i < 2; ++i) {
            output_images[i] =
                Image{ std::format("render_output_image{}", i),
                       screen_rect.extent.width,
                       screen_rect.extent.height,
                       1u,
                       1u,
                       1u,
                       VK_FORMAT_R8G8B8A8_SRGB,
                       VK_SAMPLE_COUNT_1_BIT,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };

            depth_buffers[i] = Image{
                std::format("depth_buffer_{}", i), screen_rect.extent.width, screen_rect.extent.height, 1, 1, 1, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            };
        }
    }

    const auto frame_num = Engine::frame_num();
    const auto resource_idx = frame_num % 2;

    RenderingPrimitives& primitives = this->primitives[resource_idx];

    vkWaitForFences(dev, 1, &primitives.fen_rendering_finished, true, 16'000'000);
    primitives.cmdpool.reset();

    u32 sw_img_idx;
    {
        const auto ms16 = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{ 16 }).count();
        const auto acquire_ret = vkAcquireNextImageKHR(dev, swapchain, ms16, primitives.sem_swapchain_image, nullptr, &sw_img_idx);
        if(acquire_ret != VK_SUCCESS) {
            ENG_WARN("Acquire image failed with: {}", static_cast<u32>(acquire_ret));
            return;
        }
    }

    vkResetFences(dev, 1, &primitives.fen_rendering_finished);

    if(!primitives.desc_pool) { primitives.desc_pool = descriptor_pool_allocator.allocate_pool(layouts.at(0), 0, 2); }
    descriptor_pool_allocator.reset_pool(primitives.desc_pool);

    VkDescriptorSet frame_desc_set =
        descriptor_pool_allocator.allocate_set(primitives.desc_pool, layouts.at(0).descriptor_layouts.at(0), images.size());

    static VkSampler linear_sampler = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    DescriptorSetWriter per_frame_set_writer;
    per_frame_set_writer.write(0, 0, tlas)
        .write(1, 0, rt_image.view, {}, VK_IMAGE_LAYOUT_GENERAL)
        .write(2, 0, ddgi.radiance_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
        .write(3, 0, ddgi.irradiance_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
        .write(4, 0, ddgi.visibility_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
        .write(5, 0, ddgi.probe_offsets_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL)
        .write(6, 0, ddgi.irradiance_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL)
        .write(7, 0, ddgi.visibility_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL);
    for(u32 i = 0; i < images.size(); ++i) {
        per_frame_set_writer.write(15, i, images.at(i).view, linear_sampler, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }
    per_frame_set_writer.update(frame_desc_set, layouts.at(0), 0);

    const float hx = (halton(Engine::frame_num() % 4u, 2) * 2.0 - 1.0);
    const float hy = (halton(Engine::frame_num() % 4u, 3) * 2.0 - 1.0);
    const glm::mat3 rand_mat =
        glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

    {
        float globals[16 * 4 + 12];
        const auto view = Engine::camera()->get_view();
        const auto proj = glm::perspective(glm::radians(90.0f), 2560.0f / 1440.0f, 0.01f, 10.0f); // Engine::camera()->get_projection();
        const auto inv_view = glm::inverse(view);
        const auto inv_proj = glm::inverse(proj);
        memcpy(&globals[0], &view, sizeof(glm::mat4));
        memcpy(&globals[16], &proj, sizeof(glm::mat4));
        memcpy(&globals[32], &inv_view, sizeof(glm::mat4));
        memcpy(&globals[48], &inv_proj, sizeof(glm::mat4));
        memcpy(&globals[64], &rand_mat, sizeof(glm::mat3));
        global_buffer.push_data(globals, sizeof(globals), 0ull);
    }

    ImageStatefulBarrier output_image_barrier{ output_images[sw_img_idx] };
    ImageStatefulBarrier swapchain_image_barrier{ swapchain_images[sw_img_idx] };

    auto cmd = get_primitives().cmdpool.begin_onetime();

    {
        if(flags.test_clear(RendererFlags::DIRTY_MESH_INSTANCE_TRANSFORMS_BIT)) {
            if(mesh_instance_transform_buffers[1]->capacity < mesh_instance_transform_buffers[0]->size) {
                mesh_instance_transform_buffers[1]->deallocate();
                delete mesh_instance_transform_buffers[1];
                mesh_instance_transform_buffers[1] =
                    new Buffer("mesh instance transforms", mesh_instance_transform_buffers[0]->size,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false);
            }
            std::atomic_flag f1;
            staging->send_to(GpuStagingUpload{ .dst = mesh_instance_transform_buffers[1]->buffer,
                                               .src = mesh_instance_transform_buffers[0]->buffer,
                                               .size_bytes = mesh_instance_transform_buffers[0]->size },
                             {}, {}, &f1);

            std::vector<GpuStagingUpload> uploads;
            uploads.reserve(update_positions.size());
            for(const auto& e : update_positions) {
                uploads.push_back(GpuStagingUpload{ .dst = mesh_instance_transform_buffers[1]->buffer,
                                                    .src = std::as_bytes(std::span{ &e.transform, sizeof(e.transform) }),
                                                    .dst_offset = e.idx * sizeof(e.transform),
                                                    .size_bytes = sizeof(e.transform) });
            }
            f1.wait(false);
            staging->send_to(uploads, {}, {}, &f1);
            update_positions.clear();
            mesh_instance_transform_buffers[1]->size = mesh_instance_transform_buffers[0]->size;
            std::swap(mesh_instance_transform_buffers[0], mesh_instance_transform_buffers[1]);
            f1.wait(false);
        }
    }

    {
        ddgi_buffer.push_data(&frame_num, sizeof(u32), offsetof(DDGI_Buffer, frame_num));

        swapchain_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        output_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        ImageStatefulBarrier depth_buffer_barrier{ depth_buffers[resource_idx], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
        depth_buffer_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        const u32 handle_size_aligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);

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
        hit_sbt.size = handle_size_aligned * 2;
        hit_sbt.stride = handle_size_aligned;

        vks::StridedDeviceAddressRegionKHR callable_sbt;

        const auto* window = Engine::window();
        u32 mode = 0;
        // clang-format off
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 0 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &global_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 1 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instances_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 2 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &vertex_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &index_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 4 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 5 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &tlas_mesh_offsets_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 6 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &blas_mesh_offsets_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 7 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &triangle_geo_inst_id_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 8 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instance_transform_buffers[0]->bda);
        // clang-format on

        ImageStatefulBarrier radiance_image_barrier{ ddgi.radiance_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                     VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_WRITE_BIT };
        ImageStatefulBarrier irradiance_image_barrier{ ddgi.irradiance_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT };
        ImageStatefulBarrier visibility_image_barrier{ ddgi.visibility_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                       VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT };
        ImageStatefulBarrier offset_image_barrier{ ddgi.probe_offsets_texture, VK_IMAGE_LAYOUT_GENERAL,
                                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                          pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, 0, 1, &frame_desc_set, 0, nullptr);

        // radiance pass
        mode = 1;
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdTraceRaysKHR(cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, ddgi.rays_per_probe,
                          ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z, 1);

        radiance_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.at(RenderPipelineType::DDGI_PROBE_UPDATE).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, 0, 1, &frame_desc_set, 0, nullptr);

        // irradiance pass, only need radiance texture
        mode = 0;
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.irradiance_texture.width / 8u),
                      std::ceilf((float)ddgi.irradiance_texture.height / 8u), 1u);

        // visibility pass, only need radiance texture
        mode = 1;
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           9 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.visibility_texture.width / 8u),
                      std::ceilf((float)ddgi.visibility_texture.height / 8u), 1u);

        // probe offset pass, only need radiance texture to complete
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.at(RenderPipelineType::DDGI_PROBE_OFFSET).pipeline);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.probe_offsets_texture.width / 8.0f),
                      std::ceilf((float)ddgi.probe_offsets_texture.height / 8.0f), 1u);

        irradiance_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        visibility_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        offset_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        VkRenderingAttachmentInfo r_col_att_1{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = output_images[resource_idx].view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
        };
        VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };

        VkRenderingAttachmentInfo r_dep_att{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth_buffers[resource_idx].view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .depthStencil = { 1.0f, 0 } },
        };

        VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .extent = screen_rect.extent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = r_col_atts,
            .pDepthAttachment = &r_dep_att,
        };

        VkDeviceSize vb_offsets[]{ 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, vb_offsets);
        vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.at(RenderPipelineType::DEFAULT_UNLIT).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelines.at(RenderPipelineType::DEFAULT_UNLIT).layout, 0, 1, &frame_desc_set, 0, nullptr);

        vkCmdBeginRendering(cmd, &rendering_info);
        VkRect2D r_sciss_1{ .offset = {}, .extent = { screen_rect.extent.width, screen_rect.extent.height } };
        VkViewport r_view_1{ .x = 0.0f,
                             .y = static_cast<float>(screen_rect.extent.height),
                             .width = static_cast<float>(screen_rect.extent.width),
                             .height = -static_cast<float>(screen_rect.extent.height),
                             .minDepth = 0.0f,
                             .maxDepth = 1.0f };
        vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
        vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           0 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &global_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           1 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instances_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           2 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instance_transform_buffers[0]->bda);
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi_buffer.bda);
        vkCmdDrawIndexedIndirectCount(cmd, indirect_draw_buffer.buffer, sizeof(IndirectDrawCommandBufferHeader),
                                      indirect_draw_buffer.buffer, 0ull, mesh_instances.size(),
                                      sizeof(VkDrawIndexedIndirectCommand));
        vkCmdEndRendering(cmd);

        output_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

        ImDrawData* im_draw_data = ImGui::GetDrawData();
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
                .renderArea = VkRect2D{ .offset = { 0, 0 }, .extent = { Engine::window()->width, Engine::window()->height } },
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = i_col_atts,
            };
            vkCmdBeginRendering(cmd, &imgui_rendering_info);
            vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
            vkCmdSetViewport(cmd, 0, 1, &r_view_1);
            ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
            vkCmdEndRendering(cmd);
        }

        /*  swapchain_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);*/

        /*  VkImageBlit imgui_blit_to_swapchain{
              .srcSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0,
          .layerCount = 1 }, .srcOffsets = { {}, { (int)screen_rect.extent.width, (int)screen_rect.extent.height, 1 } },
              .dstSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0,
          .layerCount = 1 }, .dstOffsets = { { screen_rect.offset.x, screen_rect.offset.y }, {
          std::min(screen_rect.offset.x + (int)screen_rect.extent.width, (int)swapchain_images[sw_img_idx].width), std::min(screen_rect.offset.y
          + (int)screen_rect.extent.height, (int)swapchain_images[sw_img_idx].height), 1 } }
          };

          vkCmdBlitImage(cmd, output_images[sw_img_idx].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         swapchain_images[sw_img_idx].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &imgui_blit_to_swapchain, VK_FILTER_LINEAR);*/

        swapchain_image_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_NONE, VK_ACCESS_NONE);

        get_primitives().cmdpool.end(cmd);

        scheduler_gq.enqueue_wait_submit(
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
        vkQueuePresentKHR(gq, &pinfo);
    }
}

Handle<RenderTexture> RendererVulkan::batch_texture(const RenderTexture& batch) {
    Handle<Image> handle =
        images.insert(Image{ batch.name, batch.width, batch.height, batch.depth, batch.mips, 1u, VK_FORMAT_R8G8B8A8_SRGB,
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

    flags.set(RendererFlags::DIRTY_MESH_INSTANCES | RendererFlags::UPLOAD_MESH_INSTANCE_TRANSFORMS_BIT);
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
    Scene* scene = Engine::scene();
    u32 idx = render_instance_idxs.at(handle);
    glm::mat4 transform = scene->get_final_transform(mesh_instances.at(render_instance_idxs.at(handle)).entity);
    update_positions.push_back(UpdatePosition{ .idx = idx, .transform = transform });
    flags.set(RendererFlags::DIRTY_MESH_INSTANCE_TRANSFORMS_BIT | RendererFlags::DIRTY_TLAS_BIT);
}

void RendererVulkan::upload_model_textures() {
    VkSemaphore acquire_sem = create_semaphore();
    VkSemaphore release_sem = create_semaphore();
    VkSemaphore transfer_done_sem = create_semaphore();
    VkCommandBuffer acquire_cmd = get_primitives().cmdpool.begin_onetime();
    VkCommandBuffer release_cmd = get_primitives().cmdpool.begin_onetime();
    VkCommandBuffer cmd = get_primitives().cmdpool.begin_onetime();
    std::vector<GpuStagingUpload> uploads;
    uploads.reserve(upload_images.size());

    for(auto& tex : upload_images) {
        Image* img = images.try_find(tex.image_handle);
        {
            VkImageMemoryBarrier img_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask = VK_ACCESS_NONE,
                .oldLayout = img->current_layout,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = gqi,
                .dstQueueFamilyIndex = tqi1,
                .image = img->image,
                .subresourceRange = { .aspectMask = img->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };
            vkCmdPipelineBarrier(release_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_NONE, {}, 0, {}, 0,
                                 {}, 1, &img_barrier);
        }
        {
            VkImageMemoryBarrier img_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask = VK_ACCESS_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = tqi1,
                .dstQueueFamilyIndex = gqi,
                .image = img->image,
                .subresourceRange = { .aspectMask = img->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };
            vkCmdPipelineBarrier(acquire_cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, 0, {}, 0,
                                 {}, 1, &img_barrier);
        }
        {
            VkImageMemoryBarrier img_barrier{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask = VK_ACCESS_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = gqi,
                .dstQueueFamilyIndex = gqi,
                .image = img->image,
                .subresourceRange = { .aspectMask = img->aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, {}, 0, {}, 0, {}, 1, &img_barrier);
        }
        uploads.push_back(GpuStagingUpload{ .src_queue_idx = gqi,
                                            .dst = img,
                                            .src = std::span{ tex.rgba_data.begin(), tex.rgba_data.end() },
                                            .dst_offset = VkOffset3D{},
                                            .dst_img_rel_sem = release_sem,
                                            .size_bytes = tex.rgba_data.size() });
    }
    get_primitives().cmdpool.end(release_cmd);
    get_primitives().cmdpool.end(acquire_cmd);
    get_primitives().cmdpool.end(cmd);

    scheduler_gq.enqueue_wait_submit({ .buffers = { release_cmd },
                                       .signals = { { release_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });

    std::atomic_flag flag;
    staging->send_to(std::span{ uploads.data(), uploads.size() }, release_sem, transfer_done_sem, &flag);
    flag.wait(false);

    vks::FenceCreateInfo fence_info{};
    VkFence fence;
    VK_CHECK(vkCreateFence(dev, &fence_info, {}, &fence));
    scheduler_gq.enqueue_wait_submit({ .buffers = { acquire_cmd },
                                       .waits = { { transfer_done_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
                                       .signals = { { acquire_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } });
    scheduler_gq.enqueue_wait_submit({ .buffers = { cmd }, .waits = { { acquire_sem, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } } }, fence);
    VK_CHECK(vkWaitForFences(dev, 1, &fence, true, 1'000'000));
    vkDestroyFence(dev, fence, {});
    destroy_semaphore(release_sem);
    destroy_semaphore(acquire_sem);
    destroy_semaphore(transfer_done_sem);
    upload_images.clear();
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

    render_instance_idxs.clear();
    render_instance_idxs.reserve(mesh_instances.size());
    for(u32 i = 0; i < mesh_instances.size(); ++i) {
        render_instance_idxs[mesh_instances.at(i).handle] = i;
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
                                                          (u32)images.find_idx(Handle<Image>{ *mat.color_texture }) });
        if(i == 0 || mesh_instances.at(i - 1).mesh != mi.mesh) {
            gpu_draw_commands.push_back(VkDrawIndexedIndirectCommand{ .indexCount = mb.index_count,
                                                                      .instanceCount = 1,
                                                                      .firstIndex = geom.index_offset + mb.index_offset,
                                                                      .vertexOffset = (i32)(geom.vertex_offset + mb.vertex_offset),
                                                                      .firstInstance = i });
        } else {
            ++gpu_draw_commands.back().instanceCount;
        }
    }

    gpu_draw_header.draw_count = gpu_draw_commands.size();
    gpu_draw_header.geometry_instance_count = mesh_instances.size();

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

void RendererVulkan::compile_shaders() {
    shaderc::Compiler c;

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

    std::filesystem::path files[]{
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "closest_hit.rchit.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "shadow.rchit.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "miss.rmiss.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "shadow.rmiss.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "raygen.rgen.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "probe_irradiance.comp",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "rtbasic" / "probe_offset.comp",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "default_unlit" / "default.vert.glsl",
        std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" / "default_unlit" / "default.frag.glsl",
    };
    shaderc_shader_kind kinds[]{
        shaderc_closesthit_shader, shaderc_closesthit_shader, shaderc_miss_shader,
        shaderc_miss_shader,       shaderc_raygen_shader,     shaderc_compute_shader,
        shaderc_compute_shader,    shaderc_vertex_shader,     shaderc_fragment_shader,
    };
    VkShaderStageFlagBits stages[]{
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
        VK_SHADER_STAGE_MISS_BIT_KHR,        VK_SHADER_STAGE_RAYGEN_BIT_KHR,      VK_SHADER_STAGE_COMPUTE_BIT,
        VK_SHADER_STAGE_COMPUTE_BIT,         VK_SHADER_STAGE_VERTEX_BIT,          VK_SHADER_STAGE_FRAGMENT_BIT,

    };
    ShaderModuleType types[]{
        ShaderModuleType::RT_BASIC_CLOSEST_HIT,
        ShaderModuleType::RT_BASIC_SHADOW_HIT,
        ShaderModuleType::RT_BASIC_MISS,
        ShaderModuleType::RT_BASIC_SHADOW_MISS,
        ShaderModuleType::RT_BASIC_RAYGEN,
        ShaderModuleType::RT_BASIC_PROBE_IRRADIANCE_COMPUTE,
        ShaderModuleType::RT_BASIC_PROBE_PROBE_OFFSET_COMPUTE,
        ShaderModuleType::DEFAULT_UNLIT_VERTEX,
        ShaderModuleType::DEFAULT_UNLIT_FRAGMENT,
    };
    std::vector<std::vector<u32>> compiled_modules;

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetGenerateDebugInfo();
    // options.AddMacroDefinition("ASDF");
    for(int i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        std::string file_str = read_file(files[i]);
        auto res = c.CompileGlslToSpv(file_str, kinds[i], files[i].filename().string().c_str(), options);

        if(res.GetCompilationStatus() != shaderc_compilation_status_success) {
            ENG_WARN("Could not compile shader : {}, because : \"{}\"", files[i].string(), res.GetErrorMessage());
        }

        compiled_modules.emplace_back(res.begin(), res.end());

        vks::ShaderModuleCreateInfo module_info;
        module_info.codeSize = compiled_modules.back().size() * sizeof(compiled_modules.back()[0]);
        module_info.pCode = compiled_modules.back().data();
        vkCreateShaderModule(dev, &module_info, nullptr, &shader_modules[types[i]].module);
        shader_modules[types[i]].stage = stages[i];
    }
}

void RendererVulkan::build_pipelines() {
    RenderPipelineLayout default_layout = RendererPipelineLayoutBuilder{}
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
        descriptor_pool_allocator.allocate_pool(imgui_layout, 0, 16, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void RendererVulkan::build_sbt() {
    const RenderPipelineWrapper& pipeline = pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST);
    const u32 handleSize = rt_props.shaderGroupHandleSize;
    const u32 handleSizeAligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);
    const u32 groupCount = static_cast<u32>(pipeline.rt_shader_group_count);
    const u32 sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    vkGetRayTracingShaderGroupHandlesKHR(dev, pipeline.pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

    const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    sbt = Buffer{ "buffer_sbt", sbtSize, bufferUsageFlags, false };
    sbt.push_data(shaderHandleStorage);
}

void RendererVulkan::create_rt_output_image() {
    const auto* window = Engine::window();
    rt_image = Image{
        "rt_image", window->width, window->height, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    };

    auto cmd = get_primitives().cmdpool.begin_onetime();
    rt_image.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                               VK_ACCESS_2_SHADER_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    get_primitives().cmdpool.end(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkQueueWaitIdle(gq);
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

    auto cmd = get_primitives().cmdpool.begin_onetime();
    vkCmdBuildAccelerationStructuresKHR(cmd, blas_geo_build_infos.size(), blas_geo_build_infos.data(), poffsets.data());
    get_primitives().cmdpool.end(cmd);
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
    for(u32 i = 0, toff = 0; i < blas_instances.size(); ++i) {
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
        blas_mesh_offsets.push_back((geom.index_offset + mb.index_offset) / 3u);

        ++toff;
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

    auto cmd = get_primitives().cmdpool.begin_onetime();
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
    get_primitives().cmdpool.end(cmd);
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

void RendererVulkan::prepare_ddgi() {
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
    ddgi.probe_dims.min *= glm::vec3{ 0.9, 0.7, 0.9 };
    ddgi.probe_dims.max *= glm::vec3{ 0.9, 0.7, 0.9 };
    ddgi.probe_distance = glm::max(ddgi.probe_dims.size().x, glm::max(ddgi.probe_dims.size().y, ddgi.probe_dims.size().z)) / 2.0f;

    ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
    ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
                          std::bit_ceil(ddgi.probe_counts.z) };
    // ddgi.probe_counts = { 16, 4, 16 };
    const auto num_probes = ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z;

    ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };

    const u32 irradiance_texture_width = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const u32 irradiance_texture_height = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.z;
    const u32 visibility_texture_width = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const u32 visibility_texture_height = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.z;

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

    ddgi.probe_offsets_texture = Image{ "ddgi probe offsets",
                                        ddgi.probe_counts.x * ddgi.probe_counts.y,
                                        ddgi.probe_counts.z,
                                        1,
                                        1,
                                        1,
                                        VK_FORMAT_R16G16B16A16_SFLOAT,
                                        VK_SAMPLE_COUNT_1_BIT,
                                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };

    auto cmd = get_primitives().cmdpool.begin_onetime();
    ddgi.radiance_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                            VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.irradiance_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                              VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    ddgi.visibility_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                              VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);

    ddgi.probe_offsets_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                                 VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);

    get_primitives().cmdpool.end(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });

    ddgi_buffer = Buffer{ "ddgi_settings_buffer", sizeof(DDGI_Buffer),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    ddgi_debug_probe_offsets_buffer =
        Buffer{ "ddgi debug probe offsets buffer", sizeof(glm::vec3) * num_probes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    DDGI_Buffer ddgi_gpu_settings{
        .radiance_tex_size = glm::ivec2{ ddgi.radiance_texture.width, ddgi.radiance_texture.height },
        .irradiance_tex_size = glm::ivec2{ ddgi.irradiance_texture.width, ddgi.irradiance_texture.height },
        .visibility_tex_size = glm::ivec2{ ddgi.visibility_texture.width, ddgi.visibility_texture.height },
        .probe_offset_tex_size = glm::ivec2{ ddgi.probe_offsets_texture.width, ddgi.probe_offsets_texture.height },
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
    };
#if 0
    ddgi_buffer_mapped->debug_probe_offsets_buffer = ddgi_debug_probe_offsets_buffer.bda;
#endif

    if(ddgi.probe_counts.y == 1) { ddgi_gpu_settings.probe_start.y += ddgi.probe_walk.y * 0.5f; }

    ddgi_buffer.push_data(&ddgi_gpu_settings, sizeof(DDGI_Buffer));

    vkQueueWaitIdle(gq);
}

VkSemaphore RendererVulkan::create_semaphore() {
    VkSemaphore sem;
    vks::SemaphoreCreateInfo info;
    VK_CHECK(vkCreateSemaphore(dev, &info, {}, &sem));
    return sem;
}

void RendererVulkan::destroy_semaphore(VkSemaphore sem) { vkDestroySemaphore(dev, sem, {}); }

Image::Image(const std::string& name, u32 width, u32 height, u32 depth, u32 mips, u32 layers, VkFormat format,
             VkSampleCountFlagBits samples, VkImageUsageFlags usage)
    : format(format), mips(mips), layers(layers), width(width), height(height), depth(depth) {
    vks::ImageCreateInfo iinfo;

    int dims = -1;
    if(width > 1) { ++dims; }
    if(height > 1) { ++dims; }
    if(depth > 1) { ++dims; }
    if(dims == -1) { dims = 1; }
    VkImageType types[]{ VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };

    iinfo.imageType = types[dims];
    iinfo.flags = {};
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

    VK_CHECK(vmaCreateImage(get_renderer().vma, &iinfo, &vmainfo, &image, &alloc, nullptr));

    _deduce_aspect(usage);
    _create_default_view(dims, usage);

    set_debug_name(image, name);
    set_debug_name(view, std::format("{}_default_view", name));
}

Image::Image(const std::string& name, VkImage image, u32 width, u32 height, u32 depth, u32 mips, u32 layers,
             VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage)
    : image{ image }, format(format), mips(mips), layers(layers), width(width), height(height), depth(depth) {
    int dims = -1;
    if(width > 1) { ++dims; }
    if(height > 1) { ++dims; }
    if(depth > 1) { ++dims; }
    if(dims == -1) { dims = 1; }

    _deduce_aspect(usage);
    _create_default_view(dims, usage);

    set_debug_name(image, name);
    set_debug_name(view, std::format("{}_default_view", name));
}

Image::Image(Image&& other) noexcept { *this = std::move(other); }

Image& Image::operator=(Image&& other) noexcept {
    if(image) { vkDestroyImage(RendererVulkan::get()->dev, image, nullptr); }
    if(view) { vkDestroyImageView(RendererVulkan::get()->dev, view, nullptr); }
    image = std::exchange(other.image, nullptr);
    alloc = std::exchange(other.alloc, nullptr);
    view = std::exchange(other.view, nullptr);
    format = other.format;
    aspect = other.aspect;
    current_layout = other.current_layout;
    width = other.width;
    height = other.height;
    depth = other.depth;
    mips = other.mips;
    layers = other.layers;
    return *this;
}

void Image::transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                              VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, bool from_undefined,
                              VkImageLayout dst_layout) {
    vks::ImageMemoryBarrier2 imgb;
    imgb.image = image;
    imgb.oldLayout = from_undefined ? VK_IMAGE_LAYOUT_UNDEFINED : current_layout;
    imgb.newLayout = dst_layout;
    imgb.srcStageMask = src_stage;
    imgb.srcAccessMask = src_access;
    imgb.dstStageMask = dst_stage;
    imgb.dstAccessMask = dst_access;
    imgb.subresourceRange = {
        .aspectMask = aspect,
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

void Image::_deduce_aspect(VkImageUsageFlags usage) {
    aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    if(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        if(format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM) {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        } else if(format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else if(format == VK_FORMAT_S8_UINT) {
            aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        } else {
            ENG_WARN("Unrecognized format for view aspect");
        }
    }
}

void Image::_create_default_view(int dims, VkImageUsageFlags usage) {
    VkImageViewType view_types[]{ VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D };

    vks::ImageViewCreateInfo ivinfo;
    ivinfo.image = image;
    ivinfo.viewType = view_types[dims];
    ivinfo.components = {};
    ivinfo.format = format;
    ivinfo.subresourceRange = { .aspectMask = aspect, .baseMipLevel = 0, .levelCount = mips, .baseArrayLayer = 0, .layerCount = 1 };

    VK_CHECK(vkCreateImageView(get_renderer().dev, &ivinfo, nullptr, &view));
}

RenderPipelineLayout RendererPipelineLayoutBuilder::build() {
    std::vector<VkDescriptorSetLayout> vk_layouts;
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindings;
    std::vector<std::vector<VkDescriptorBindingFlags>> binding_flags;
    for(u32 i = 0; i < descriptor_layouts.size(); ++i) {
        DescriptorLayout& desc_layout = descriptor_layouts.at(i);

        if(desc_layout.bindings.empty()) { break; }

        if(desc_layout.last_binding_of_variable_count) {
            desc_layout.binding_flags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        }

        vks::DescriptorSetLayoutBindingFlagsCreateInfo flags_info;
        flags_info.bindingCount = desc_layout.binding_flags.size();
        flags_info.pBindingFlags = desc_layout.binding_flags.data();

        vks::DescriptorSetLayoutCreateInfo info;
        info.bindingCount = desc_layout.bindings.size();
        info.pBindings = desc_layout.bindings.data();
        info.flags = descriptor_layout_flags.at(i);
        info.pNext = &flags_info;

        VK_CHECK(vkCreateDescriptorSetLayout(get_renderer().dev, &info, nullptr, &desc_layout.layout));

        vk_layouts.push_back(desc_layout.layout);
        bindings.push_back(desc_layout.bindings);
        binding_flags.push_back(desc_layout.binding_flags);
    }

    vks::PipelineLayoutCreateInfo layout_info;
    VkPushConstantRange push_constant_range;
    if(push_constants_size > 0) {
        push_constant_range.stageFlags = push_constants_stage;
        push_constant_range.offset = 0;
        push_constant_range.size = push_constants_size;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant_range;
    }
    layout_info.setLayoutCount = vk_layouts.size();
    layout_info.pSetLayouts = vk_layouts.data();

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(get_renderer().dev, &layout_info, nullptr, &layout));

    return RenderPipelineLayout{ layout,
                                   { vk_layouts.begin(), vk_layouts.end() },
                                   { descriptor_layout_flags.begin(), descriptor_layout_flags.end() },
                                   bindings,
                                   binding_flags };
}

VkPipeline RendererComputePipelineBuilder::build() {
    vks::ComputePipelineCreateInfo info;
    info.stage = vks::PipelineShaderStageCreateInfo{};
    info.stage.module = module;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.pName = "main";
    info.layout = layout;

    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(static_cast<RendererVulkan*>(Engine::renderer())->dev, nullptr, 1, &info, nullptr, &pipeline));
    return pipeline;
}

VkPipeline RendererRaytracingPipelineBuilder::build() {
    vks::RayTracingPipelineCreateInfoKHR info;
    info.stageCount = stages.size();
    info.pStages = stages.data();
    info.groupCount = shader_groups.size();
    info.pGroups = shader_groups.data();
    info.maxPipelineRayRecursionDepth = recursion_depth;
    info.layout = layout;

    VkPipeline pipeline;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(static_cast<RendererVulkan*>(Engine::renderer())->dev, nullptr, nullptr, 1,
                                            &info, nullptr, &pipeline));
    return pipeline;
}

VkPipeline RendererGraphicsPipelineBuilder::build() {
    std::vector<vks::PipelineShaderStageCreateInfo> pStages;
    pStages.reserve(shader_stages.size());
    for(const auto& [stage, module] : shader_stages) {
        auto& info = pStages.emplace_back();
        info.stage = stage;
        info.module = module;
        info.pName = "main";
    }

    vks::PipelineVertexInputStateCreateInfo pVertexInputState;
    pVertexInputState.vertexAttributeDescriptionCount = vertex_inputs.size();
    pVertexInputState.pVertexAttributeDescriptions = vertex_inputs.data();
    pVertexInputState.vertexBindingDescriptionCount = vertex_bindings.size();
    pVertexInputState.pVertexBindingDescriptions = vertex_bindings.data();

    vks::PipelineInputAssemblyStateCreateInfo pInputAssemblyState;
    pInputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pInputAssemblyState.primitiveRestartEnable = false;

    vks::PipelineTessellationStateCreateInfo pTessellationState;

    vks::PipelineViewportStateCreateInfo pViewportState;
    pViewportState.scissorCount = scissor_count;
    pViewportState.viewportCount = viewport_count;

    vks::PipelineRasterizationStateCreateInfo pRasterizationState;
    pRasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    pRasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
    pRasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pRasterizationState.lineWidth = 1.0f;

    vks::PipelineMultisampleStateCreateInfo pMultisampleState;
    pMultisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    vks::PipelineDepthStencilStateCreateInfo pDepthStencilState;
    pDepthStencilState.depthTestEnable = depth_test;
    pDepthStencilState.depthWriteEnable = depth_write;
    pDepthStencilState.depthCompareOp = depth_op;
    pDepthStencilState.depthBoundsTestEnable = false;
    pDepthStencilState.stencilTestEnable = stencil_test;
    pDepthStencilState.front = stencil_front;
    pDepthStencilState.back = stencil_back;

    vks::PipelineColorBlendStateCreateInfo pColorBlendState;
    if(color_blending_attachments.empty()) {
        auto& att = color_blending_attachments.emplace_back();
        att.blendEnable = false;
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    pColorBlendState.logicOpEnable = false;
    pColorBlendState.attachmentCount = color_blending_attachments.size();
    pColorBlendState.pAttachments = color_blending_attachments.data();

    vks::PipelineDynamicStateCreateInfo pDynamicState;
    pDynamicState.dynamicStateCount = dynamic_states.size();
    pDynamicState.pDynamicStates = dynamic_states.data();

    vks::PipelineRenderingCreateInfo pDynamicRendering;
    pDynamicRendering.colorAttachmentCount = color_attachment_formats.size();
    pDynamicRendering.pColorAttachmentFormats = color_attachment_formats.data();
    pDynamicRendering.depthAttachmentFormat = depth_attachment_format;
    pDynamicRendering.stencilAttachmentFormat = stencil_attachment_format;

    vks::GraphicsPipelineCreateInfo info;
    info.stageCount = pStages.size();
    info.pStages = pStages.data();
    info.pVertexInputState = &pVertexInputState;
    info.pInputAssemblyState = &pInputAssemblyState;
    info.pTessellationState = &pTessellationState;
    info.pViewportState = &pViewportState;
    info.pRasterizationState = &pRasterizationState;
    info.pMultisampleState = &pMultisampleState;
    info.pDepthStencilState = &pDepthStencilState;
    info.pColorBlendState = &pColorBlendState;
    info.pDynamicState = &pDynamicState;
    info.layout = layout;
    info.pNext = &pDynamicRendering;

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(get_renderer().dev, nullptr, 1, &info, nullptr, &pipeline));

    return pipeline;
}

DescriptorSetWriter& DescriptorSetWriter::write(u32 binding, u32 array_element, const Image& image, VkImageLayout layout) {
    writes.emplace_back(binding, array_element, WriteImage{ image.view, layout, VkSampler{} });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(u32 binding, u32 array_element, const Image& image, VkSampler sampler,
                                                VkImageLayout layout) {
    writes.emplace_back(binding, array_element, WriteImage{ image.view, layout, sampler });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(u32 binding, u32 array_element, VkImageView image, VkSampler sampler,
                                                VkImageLayout layout) {
    writes.emplace_back(binding, array_element, WriteImage{ image, layout, sampler });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(u32 binding, u32 array_element, const Buffer& buffer, u32 offset, u32 range) {
    writes.emplace_back(binding, array_element, WriteBuffer{ buffer.buffer, offset, range });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(u32 binding, u32 array_element, const VkAccelerationStructureKHR ac) {
    writes.emplace_back(binding, array_element, ac);
    return *this;
}

bool DescriptorSetWriter::update(VkDescriptorSet set, const RenderPipelineLayout& layout, u32 set_idx) {
    const std::vector<VkDescriptorSetLayoutBinding>& set_bindings = layout.bindings.at(set_idx);

    std::vector<std::variant<VkDescriptorImageInfo, VkDescriptorBufferInfo, vks::WriteDescriptorSetAccelerationStructureKHR>> write_infos;
    std::vector<vks::WriteDescriptorSet> write_sets;
    write_sets.reserve(writes.size());
    write_infos.reserve(writes.size());

    for(const WriteData& wd : writes) {
        vks::WriteDescriptorSet& write_set = write_sets.emplace_back();
        write_set.dstSet = set;
        write_set.dstBinding = wd.binding;
        write_set.descriptorCount = 1;
        write_set.dstArrayElement = wd.array_element;
        write_set.descriptorType =
            std::find_if(set_bindings.begin(), set_bindings.end(), [&wd](const VkDescriptorSetLayoutBinding& b) {
                return b.binding == wd.binding;
            })->descriptorType;
        std::visit(Visitor{ [&](const WriteImage& p) {
                               write_set.pImageInfo = &std::get<0>(write_infos.emplace_back(VkDescriptorImageInfo{
                                   .sampler = p.sampler,
                                   .imageView = p.view,
                                   .imageLayout = p.layout,
                               }));
                           },
                            [&](const WriteBuffer& p) {
                                write_set.pBufferInfo = &std::get<1>(write_infos.emplace_back(VkDescriptorBufferInfo{
                                    .buffer = p.buffer,
                                    .offset = p.offset,
                                    .range = p.range,
                                }));
                            },
                            [&](const VkAccelerationStructureKHR& p) {
                                vks::WriteDescriptorSetAccelerationStructureKHR write_acc;
                                write_acc.accelerationStructureCount = 1;
                                write_acc.pAccelerationStructures = &p;
                                write_set.pNext = &std::get<2>(write_infos.emplace_back(write_acc));
                            } },
                   wd.payload);
    }

    vkUpdateDescriptorSets(get_renderer().dev, write_sets.size(), write_sets.data(), 0, nullptr);
    return true;
}

VkSampler SamplerStorage::get_sampler() {
    vks::SamplerCreateInfo sampler_info;
    return get_sampler(sampler_info);
}

VkSampler SamplerStorage::get_sampler(VkFilter filter, VkSamplerAddressMode address) {
    vks::SamplerCreateInfo sampler_info;
    sampler_info.addressModeU = address;
    sampler_info.addressModeV = address;
    sampler_info.addressModeW = address;
    sampler_info.minFilter = filter;
    sampler_info.magFilter = filter;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.maxLod = sampler_info.minLod + 1.0f;
    return get_sampler(sampler_info);
}

VkSampler SamplerStorage::get_sampler(vks::SamplerCreateInfo info) {
    for(const auto& [c, s] : samplers) {
        if(c.magFilter != info.magFilter) { continue; }
        if(c.minFilter != info.minFilter) { continue; }
        if(c.mipmapMode != info.mipmapMode) { continue; }
        if(c.addressModeU != info.addressModeU) { continue; }
        if(c.addressModeV != info.addressModeV) { continue; }
        if(c.addressModeW != info.addressModeW) { continue; }
        if(c.mipLodBias != info.mipLodBias) { continue; }
        if(c.anisotropyEnable != info.anisotropyEnable) { continue; }
        if(c.maxAnisotropy != info.maxAnisotropy) { continue; }
        if(c.compareEnable != info.compareEnable) { continue; }
        if(c.compareOp != info.compareOp) { continue; }
        if(c.minLod != info.minLod) { continue; }
        if(c.maxLod != info.maxLod) { continue; }
        if(c.borderColor != info.borderColor) { continue; }
        if(c.unnormalizedCoordinates != info.unnormalizedCoordinates) { continue; }
        return s;
    }

    VkSampler sampler;
    VK_CHECK(vkCreateSampler(get_renderer().dev, &info, nullptr, &sampler));

    samplers.emplace_back(info, sampler);
    return sampler;
}

VkDescriptorPool DescriptorPoolAllocator::allocate_pool(const RenderPipelineLayout& layout, u32 set, u32 max_sets,
                                                        VkDescriptorPoolCreateFlags flags) {
    const std::vector<VkDescriptorSetLayoutBinding>& bindings = layout.bindings.at(set);
    const std::vector<VkDescriptorBindingFlags>& binding_flags = layout.binding_flags.at(set);

    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(bindings.size());

    const auto find_size_of_type = [&sizes](VkDescriptorType type) -> VkDescriptorPoolSize& {
        for(auto& s : sizes) {
            if(s.type == type) { return s; }
        }
        return sizes.emplace_back(VkDescriptorPoolSize{ .type = type, .descriptorCount = 0u });
    };

    for(u32 i = 0; i < bindings.size(); ++i) {
        if(binding_flags.at(i) & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
            find_size_of_type(bindings.at(i).descriptorType).descriptorCount += bindings.at(i).descriptorCount;
        } else {
            find_size_of_type(bindings.at(i).descriptorType).descriptorCount += bindings.at(i).descriptorCount * max_sets;
        }
    }

    vks::DescriptorPoolCreateInfo info;
    info.maxSets = max_sets;
    info.flags = flags;
    info.poolSizeCount = sizes.size();
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(get_renderer().dev, &info, nullptr, &pool));

    pools.emplace(pool, PoolDescriptor{});

    return pool;
}

VkDescriptorSet DescriptorPoolAllocator::allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout, u32 variable_count) {
    auto& pool_desc = pools.at(pool);
    for(auto& set : pool_desc.sets) {
        if(set.free && set.layout == layout) {
            set.free = false;
            return set.set;
        }
    }

    vks::DescriptorSetVariableDescriptorCountAllocateInfo variable_info;
    variable_info.descriptorSetCount = 1;
    variable_info.pDescriptorCounts = &variable_count;

    vks::DescriptorSetAllocateInfo info;
    info.descriptorPool = pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    if(variable_count > 0) { info.pNext = &variable_info; }

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(get_renderer().dev, &info, &set));

    pool_desc.sets.emplace_back(set, layout, false);

    return set;
}

void DescriptorPoolAllocator::reset_pool(VkDescriptorPool pool) {
    if(!pool) { return; }
    for(auto& e : pools.at(pool).sets) {
        e.free = true;
    }
    // VK_CHECK(vkResetDescriptorPool(get_renderer().dev, pool, {})); // resetting actualy makes descriptor sets invalid - as opposed to commandpool
}

QueueScheduler::QueueScheduler(VkQueue queue) : vkqueue{ queue } { assert(queue); }

void QueueScheduler::enqueue(const RecordingSubmitInfo& info, VkFence fence) {
    struct BatchedSubmits {
        std::vector<vks::CommandBufferSubmitInfo> info_submits;
        std::vector<vks::SemaphoreSubmitInfo> info_waits;
        std::vector<vks::SemaphoreSubmitInfo> info_signals;
    };

    BatchedSubmits batch;
    batch.info_submits.reserve(info.buffers.size());
    batch.info_waits.reserve(info.waits.size());
    batch.info_signals.reserve(info.signals.size());

    for(auto& e : info.buffers) {
        vks::CommandBufferSubmitInfo info;
        info.commandBuffer = e;
        batch.info_submits.push_back(info);
    }
    for(auto& e : info.waits) {
        vks::SemaphoreSubmitInfo info;
        info.semaphore = e.first;
        info.stageMask = e.second;
        batch.info_waits.push_back(info);
    }
    for(auto& e : info.signals) {
        vks::SemaphoreSubmitInfo info;
        info.semaphore = e.first;
        info.stageMask = e.second;
        batch.info_signals.push_back(info);
    }

    VkSubmitInfo2 submit_info{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                               .waitSemaphoreInfoCount = (u32)batch.info_waits.size(),
                               .pWaitSemaphoreInfos = batch.info_waits.data(),
                               .commandBufferInfoCount = (u32)batch.info_submits.size(),
                               .pCommandBufferInfos = batch.info_submits.data(),
                               .signalSemaphoreInfoCount = (u32)batch.info_signals.size(),
                               .pSignalSemaphoreInfos = batch.info_signals.data() };
    VK_CHECK(vkQueueSubmit2(vkqueue, 1, &submit_info, fence));
}

void QueueScheduler::enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence) { enqueue(info, fence); }

CommandPool::CommandPool(u32 queue_index, VkCommandPoolCreateFlags flags) {
    vks::CommandPoolCreateInfo info;
    info.flags = flags;
    info.queueFamilyIndex = queue_index;
    VK_CHECK(vkCreateCommandPool(RendererVulkan::get()->dev, &info, {}, &cmdpool));
}

CommandPool::~CommandPool() noexcept {
    if(cmdpool) { vkDestroyCommandPool(RendererVulkan::get()->dev, cmdpool, nullptr); }
}

CommandPool::CommandPool(CommandPool&& other) noexcept { *this = std::move(other); }

CommandPool& CommandPool::operator=(CommandPool&& other) noexcept {
    cmdpool = std::exchange(other.cmdpool, nullptr);
    return *this;
}

VkCommandBuffer CommandPool::allocate(VkCommandBufferLevel level) {
    auto it = buffers.begin();
    if(it != buffers.end() && it->second) {
        VkCommandBuffer buffer = it->first;
        it->second = false;
        std::sort(it, buffers.end(), [](auto&& a, auto&& b) { return a.second > b.second; });
        return buffer;
    }

    vks::CommandBufferAllocateInfo info;
    info.commandBufferCount = 1;
    info.commandPool = cmdpool;
    info.level = level;
    VkCommandBuffer buffer;
    VK_CHECK(vkAllocateCommandBuffers(RendererVulkan::get()->dev, &info, &buffer));
    buffers.emplace_back(buffer, false);
    return buffer;
}

VkCommandBuffer CommandPool::begin(VkCommandBufferUsageFlags flags, VkCommandBufferLevel level) {
    vks::CommandBufferBeginInfo info;
    info.flags = flags;
    VkCommandBuffer buffer = allocate(level);
    VK_CHECK(vkBeginCommandBuffer(buffer, &info));
    return buffer;
}

VkCommandBuffer CommandPool::begin_onetime(VkCommandBufferLevel level) {
    return begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void CommandPool::end(VkCommandBuffer buffer) { VK_CHECK(vkEndCommandBuffer(buffer)); }

void CommandPool::reset() {
    vkResetCommandPool(RendererVulkan::get()->dev, cmdpool, {});
    for(auto& e : buffers) {
        e.second = true;
    }
}
