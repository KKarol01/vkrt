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

void RendererVulkan::init() {
    initialize_vulkan();
    create_rt_output_image();
    compile_shaders();
    build_pipelines();
    build_sbt();
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
        if(Engine::frame_num() < 100) { initialize_ddgi(); }
    }
    if(flags.test_clear(RendererFlags::REFIT_TLAS_BIT)) { refit_tlas(); }
    // if(flags.test_clear(RendererFlags::UPLOAD_MESH_INSTANCE_TRANSFORMS_BIT)) { upload_transforms(); }
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

    if(Engine::frame_num() > 0) {
        for(u32 i = 0; i < ddgi.debug_probes.size(); ++i) {
            auto h = Handle<Entity>{ *ddgi.debug_probes.at(i) };
            auto& t = Engine::ec()->get<cmps::Transform>(h);
            const auto tv =
                ddgi.probe_start +
                ddgi.probe_walk * glm::vec3{ i % ddgi.probe_counts.x, (i / ddgi.probe_counts.x) % ddgi.probe_counts.y,
                                             i / (ddgi.probe_counts.x * ddgi.probe_counts.y) } +
                ((glm::vec3*)ddgi.debug_probe_offsets_buffer.mapped)[i];
            t.transform = glm::translate(glm::mat4{ 1.0f }, tv) * glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 0.2f });
            Engine::scene()->update_transform(h);
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
        const auto proj = glm::perspective(glm::radians(45.0f), 1024.0f/768.0f, 0.01f, 20.0f); // Engine::camera()->get_projection();
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

        /* TODO:
            for later: put appropriate barriers for transform buffer[0] (it's being used by the previous frame)
            either that or use second buffer if it matches the first one (how to make it match efficiently)
        */

        /*if(mesh_instance_transform_buffers[1]->capacity < mesh_instance_transform_buffers[0]->size) {
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
        f1.wait(false);*/
    }

    if(tlas && ddgi.buffer.buffer) {
        ddgi.buffer.push_data(&frame_num, sizeof(u32), offsetof(DDGI::GPULayout, frame_num));

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
        vkCmdPushConstants(cmd, pipelines.at(RenderPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 4 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi.buffer.bda);
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
    }

    // default pass
    {
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
                           3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi.buffer.bda);
        vkCmdDrawIndexedIndirectCount(cmd, indirect_draw_buffer.buffer, sizeof(IndirectDrawCommandBufferHeader),
                                      indirect_draw_buffer.buffer, 0ull, max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
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
    }

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
                                                      .color_texture_idx = (u32)images.find_idx(Handle<Image>{ *mat.color_texture }) });
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

void RendererVulkan::initialize_ddgi() {
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
    const auto dim_scaling = glm::vec3{0.95, 0.8, 0.95};
    ddgi.probe_dims.min *= dim_scaling;
    ddgi.probe_dims.max *= dim_scaling;
    ddgi.probe_distance = 1.3;

    ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
    ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
                          std::bit_ceil(ddgi.probe_counts.z) };
    //ddgi.probe_counts = {16, 4, 8};
    const auto num_probes = ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z;

    ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };
    //ddgi.probe_walk = {ddgi.probe_walk.x, 4.0f, ddgi.probe_walk.z};

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

    ddgi.buffer = Buffer{ "ddgi_settings_buffer", sizeof(DDGI::GPULayout),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };
    ddgi.debug_probe_offsets_buffer =
        Buffer{ "ddgi debug probe offsets buffer", sizeof(DDGI::GPUProbeOffsetsLayout) * num_probes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };

    DDGI::GPULayout ddgi_gpu_settings{
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

VkSemaphore RendererVulkan::create_semaphore() {
    VkSemaphore sem;
    vks::SemaphoreCreateInfo info;
    VK_CHECK(vkCreateSemaphore(dev, &info, {}, &sem));
    return sem;
}

void RendererVulkan::destroy_semaphore(VkSemaphore sem) { vkDestroySemaphore(dev, sem, {}); }