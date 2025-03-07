#include <numeric>
#include <fstream>
#include <utility>
#include <set>
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
#include <stb/stb_include.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"
#include "utils.hpp"
#include "assets/shaders/bindless_structures.inc.glsl"

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
    initialize_resources();
    initialize_imgui();
    Engine::get().add_on_window_resize_callback([this] {
        on_window_resize();
        return true;
    });
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

    const auto* window = Engine::get().window;

    auto surface_info = Vks(VkWin32SurfaceCreateInfoKHR{
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = glfwGetWin32Window(window->window),
    });
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
                        .prefer_gpu_device_type()
                        .require_present()
                        .select();
    if(!phys_ret) { throw std::runtime_error{ "Failed to select Vulkan Physical Device. Error: " }; }

    vkb::DeviceBuilder device_builder{ phys_ret.value() };

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

    auto dev_ret = device_builder.add_pNext(&dev_2_features)
                       .add_pNext(&dyn_features)
                       .add_pNext(&synch2_features)
                       .add_pNext(&dev_vk12_features)
                       .add_pNext(&acc_features)
                       .add_pNext(&rtpp_features)
                       .add_pNext(&rayq_features)
                       .build();
    if(!dev_ret) { throw std::runtime_error{ "Failed to create Vulkan device. Error: " }; }
    vkb::Device vkb_device = dev_ret.value();
    volkLoadDevice(vkb_device.device);

    VkDevice device = vkb_device.device;

    auto pdev_props = Vks(VkPhysicalDeviceProperties2{
        .pNext = &rt_props,
    });
    rt_props.pNext = &rt_acc_props;
    vkGetPhysicalDeviceProperties2(phys_ret->physical_device, &pdev_props);

    instance = vkb_inst.instance;
    dev = device;
    pdev = phys_ret->physical_device;
    gq = Queue{ .queue = vkb_device.get_queue(vkb::QueueType::graphics).value(),
                .idx = vkb_device.get_queue_index(vkb::QueueType::graphics).value() };
    screen_rect = { .w = window->width, .h = window->height };

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

void RendererVulkan::initialize_imgui() {
    IMGUI_CHECKVERSION();
    void* user_data;
    Engine::get().ui.context->imgui_ctx = ImGui::CreateContext();
    ImGui::GetAllocatorFunctions(&Engine::get().ui.context->alloc_cbs.imgui_alloc,
                                 &Engine::get().ui.context->alloc_cbs.imgui_free, &user_data);
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Engine::get().window->window, true);

    VkFormat color_formats[]{ VK_FORMAT_R8G8B8A8_SRGB };

    VkDescriptorPoolSize sizes[]{ { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
    auto imgui_dpool_info = Vks(VkDescriptorPoolCreateInfo{
        .maxSets = 1024,
        .poolSizeCount = 1,
        .pPoolSizes = sizes,
    });
    VkDescriptorPool imgui_dpool;
    VK_CHECK(vkCreateDescriptorPool(dev, &imgui_dpool_info, nullptr, &imgui_dpool));

    ImGui_ImplVulkan_InitInfo init_info = { 
        .Instance = instance,
        .PhysicalDevice = pdev,
        .Device = dev,
        .QueueFamily = gq.idx,
        .Queue = gq.queue,
        .DescriptorPool = imgui_dpool,
        .MinImageCount = (uint32_t)frame_datas.size(),
        .ImageCount = (uint32_t)frame_datas.size(),
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

    auto cmdimgui = get_frame_data().cmdpool->begin_onetime();
    ImGui_ImplVulkan_CreateFontsTexture();
    get_frame_data().cmdpool->end(cmdimgui);
    gq.submit(QueueSubmission{ .cmds = { cmdimgui } });
    gq.wait_idle();
}

void RendererVulkan::initialize_resources() {
    {
        VkDescriptorSetLayoutBinding bindings[]{
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 65536, VK_SHADER_STAGE_ALL },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 65536, VK_SHADER_STAGE_ALL },
            { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536, VK_SHADER_STAGE_ALL },
            { 3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 65536, VK_SHADER_STAGE_ALL },
        };

        auto layout_info = Vks(VkDescriptorSetLayoutCreateInfo{
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
            .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
            .pBindings = bindings,
        });
        VK_CHECK(vkCreateDescriptorSetLayout(get_renderer().dev, &layout_info, nullptr, &bindless_layout.descriptor_layout));

        VkPushConstantRange pc_range{ VK_SHADER_STAGE_ALL, 0, 128 };

        auto info = Vks(VkPipelineLayoutCreateInfo{
            .setLayoutCount = 1,
            .pSetLayouts = &bindless_layout.descriptor_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pc_range,
        });
        VK_CHECK(vkCreatePipelineLayout(get_renderer().dev, &info, nullptr, &bindless_layout.layout));

        VkDescriptorPoolSize sizes[]{
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256 },
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 16 },
        };
        auto pool_info = Vks(VkDescriptorPoolCreateInfo{
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 2,
            .poolSizeCount = sizeof(sizes) / sizeof(sizes[0]),
            .pPoolSizes = sizes,
        });
        VkDescriptorBindingFlags bflags_flags[]{
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
        };
        auto bflags = Vks(VkDescriptorSetLayoutBindingFlagsCreateInfo{
            .bindingCount = sizeof(bflags_flags) / sizeof(bflags_flags[0]), .pBindingFlags = bflags_flags });
        VK_CHECK(vkCreateDescriptorPool(get_renderer().dev, &pool_info, {}, &bindless_pool));
    }

    staging_buffer = new StagingBuffer{};
    vertex_positions_buffer = make_buffer("vertex_positions_buffer", 0ull,
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    vertex_attributes_buffer = make_buffer("vertex_attributes_buffer", 0ull,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    index_buffer = make_buffer("index_buffer", 0ull,
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

    auto samp_ne = samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    auto samp_ll = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    auto samp_lr = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // ddgi.radiance_texture = make_image(Image{ "ddgi radiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
    //                                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                    VK_IMAGE_LAYOUT_GENERAL);
    // make_image(ddgi.radiance_texture, VK_IMAGE_LAYOUT_GENERAL, samp_ll);

    // ddgi.irradiance_texture = make_image(Image{ "ddgi irradiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
    //                                             VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                      VK_IMAGE_LAYOUT_GENERAL);
    // make_image(ddgi.irradiance_texture, VK_IMAGE_LAYOUT_GENERAL, samp_ll);

    // ddgi.visibility_texture = make_image(Image{ "ddgi visibility", 1, 1, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
    //                                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                      VK_IMAGE_LAYOUT_GENERAL);
    // make_image(ddgi.visibility_texture, VK_IMAGE_LAYOUT_GENERAL, samp_ll);

    // ddgi.probe_offsets_texture = make_image(Image{ "ddgi probe offsets", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
    //                                                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
    //                                         VK_IMAGE_LAYOUT_GENERAL);
    // make_image(ddgi.probe_offsets_texture, VK_IMAGE_LAYOUT_GENERAL, samp_ll);

    for(uint32_t i = 0; i < frame_datas.size(); ++i) {
        auto& fd = frame_datas[i];
        CommandPool* cmdgq1 = &cmdpools.emplace_back(CommandPool{ gq.idx });
        fd.sem_swapchain = Semaphore{ dev, false };
        fd.sem_rendering_finished = Semaphore{ dev, false };
        fd.fen_rendering_finished = Fence{ dev, true };
        fd.cmdpool = cmdgq1;
        fd.constants = make_buffer(std::format("constants_{}", i), 512ul,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        transform_buffers[i] = make_buffer(std::format("transform_buffer_{}", i), 0ull,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    }

    vsm_data_buffer = make_buffer("vms buffer", sizeof(GPUVsmBuffer) + 64 * 64 * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    GPUVsmBuffer vsm_constants{
        .dir_light_view = glm::mat4{ 1.0f },
        .num_pages_xy = 64,
        .max_clipmap_index = 0,
        .texel_resolution = 1024.0f * 8.0f,
    };
    send_to(vsm_data_buffer, 0, &vsm_constants, sizeof(vsm_constants));

    vsm_free_allocs_buffer = make_buffer("vms alloc buffer", sizeof(GPUVsmAllocBuffer), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    GPUVsmAllocBuffer vsm_allocs{ .max_allocs = vsm_constants.num_pages_xy * vsm_constants.num_pages_xy, .alloc_head = 0 };
    send_to(vsm_free_allocs_buffer, 0, &vsm_allocs, sizeof(vsm_allocs));

    vsm_shadow_map_0 =
        make_image("vsm image", VK_FORMAT_D32_SFLOAT, VK_IMAGE_TYPE_2D, { 1024 * 8, 1024 * 8, 1 }, 1, 1,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    vsm_dir_light_page_table = make_image("vsm dir light 0 page table", VK_FORMAT_R32_UINT, VK_IMAGE_TYPE_2D,
                                          { vsm_constants.num_pages_xy, vsm_constants.num_pages_xy, 1 }, 1, 1,
                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    vsm_dir_light_page_table_rgb8 =
        make_image("vsm dir light 0 page table rgb8", VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D,
                   { vsm_constants.num_pages_xy, vsm_constants.num_pages_xy, 1 }, 1, 1,
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);

    create_window_sized_resources();

    {
        std::vector<std::filesystem::path> shaders;
        for(auto it : std::filesystem::recursive_directory_iterator{ std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" }) {
            if(!it.is_regular_file()) { continue; }
            shaders.push_back(it.path());
        }
        shader_storage.precompile_shaders(shaders);
    }

    auto allocate_info = Vks(VkDescriptorSetAllocateInfo{
        .descriptorPool = bindless_pool, .descriptorSetCount = 1, .pSetLayouts = &bindless_layout.descriptor_layout });
    VK_CHECK(vkAllocateDescriptorSets(dev, &allocate_info, &bindless_set));

    build_render_graph();
}

void RendererVulkan::create_window_sized_resources() {
    swapchain.create(frame_datas.size(), screen_rect.w, screen_rect.h);
    for(auto i = 0; i < frame_datas.size(); ++i) {
        auto& fd = frame_datas.at(i);

        fd.gbuffer.color_image = make_image(std::format("g_color_{}", i), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TYPE_2D,
                                            VkExtent3D{ (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1 }, 1, 1,
                                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        fd.gbuffer.depth_buffer_image =
            make_image(std::format("g_depth_{}", i), VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_TYPE_2D,
                       VkExtent3D{ (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1 }, 1, 1,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

        /*make_image(&fd.gbuffer.color_image,
                   Image{ std::format("g_color_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                          VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        make_image(&fd.gbuffer.view_space_positions_image,
                   Image{ std::format("g_view_pos_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                          VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });
        make_image(&fd.gbuffer.view_space_normals_image,
                   Image{ std::format("g_view_nor_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                          VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        make_image(&fd.gbuffer.depth_buffer_image,
                   Image{ std::format("g_depth_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                          VK_FORMAT_D16_UNORM, VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        make_image(&fd.gbuffer.ambient_occlusion_image,
                   Image{ std::format("ao_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
                          VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });*/
    }
}

void RendererVulkan::build_render_graph() {
    for(auto& fd : frame_datas) {
        fd.render_graph.passes.clear();
        fd.render_graph
        .add_pass(rendergraph::RenderPass{
            .accesses = { 
                rendergraph::Access{
                    { *get_renderer().vsm_dir_light_page_table, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL
                },
            },
            .shaders = { "vsm/clear_page.comp.glsl" },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                auto& r = get_renderer();
                uint32_t bindless_indices[]{
                    r.get_bindless_index(r.vsm_dir_light_page_table, BindlessType::STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL, nullptr),
                    64
                };
                vkCmdPushConstants(cmd, r.bindless_layout.layout, VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
                vkCmdBindDescriptorSets(cmd, pass.pipeline_bind_point, r.bindless_layout.layout, 0, 1, &r.bindless_set, 0, nullptr);
                vkCmdDispatch(cmd, 64/8, 64/8, 1);
            } })
        .add_pass(rendergraph::RenderPass{
            .accesses = { 
                rendergraph::Access{
                    { *fd.gbuffer.depth_buffer_image, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::READ_WRITE_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
                },
            },
            .shaders = { "vsm/zprepass.vert.glsl", "vsm/zprepass.frag.glsl" },
            .pipeline_settings =
                rendergraph::RasterizationSettings{ .num_col_formats = 0, .dep_format = VK_FORMAT_D24_UNORM_S8_UINT, .depth_test = true },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                auto& r = get_renderer();
                const auto r_dep_att = Vks(VkRenderingAttachmentInfo{
                    .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = { .depthStencil = { 1.0f, 0 } },
                });
                const auto rendering_info = Vks(VkRenderingInfo{
                    .renderArea = { .extent = { .width = (uint32_t)r.screen_rect.w, .height = (uint32_t)r.screen_rect.h } },
                    .layerCount = 1,
                    .colorAttachmentCount = 0,
                    .pColorAttachments = nullptr,
                    .pDepthAttachment = &r_dep_att,
                });
                vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd, pass.pipeline_bind_point, r.bindless_layout.layout, 0, 1, &r.bindless_set, 0, nullptr);
                vkCmdBeginRendering(cmd, &rendering_info);
                VkRect2D r_sciss_1 = rendering_info.renderArea;
                VkViewport r_view_1{ .x = 0.0f,
                                     .y = (float)rendering_info.renderArea.extent.height,
                                     .width = (float)rendering_info.renderArea.extent.width,
                                     .height = -(float)rendering_info.renderArea.extent.height,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f };
                vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
                vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
                uint32_t bindless_indices[]{
                    r.get_bindless_index(r.index_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.get_frame_data().constants, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vertex_positions_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.transform_buffers[0], BindlessType::STORAGE_BUFFER),
                };
                vkCmdPushConstants(cmd, r.bindless_layout.layout, VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
                vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                              sizeof(IndirectDrawCommandBufferHeader),
                                              r.get_buffer(r.indirect_draw_buffer).buffer, 0ull, r.max_draw_count,
                                              sizeof(VkDrawIndexedIndirectCommand));
                vkCmdEndRendering(cmd);
        } })
        .add_pass(rendergraph::RenderPass{ 
            .accesses = {
                rendergraph::Access{
                    { *fd.gbuffer.color_image, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::WRITE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                },
                rendergraph::Access{
                    { *fd.gbuffer.depth_buffer_image, rendergraph::ResourceType::COLOR_ATTACHMENT },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                },
                rendergraph::Access{
                    { *get_renderer().vsm_shadow_map_0, rendergraph::ResourceType::STORAGE_IMAGE },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,
                },
            },
            .shaders = {
                "default_unlit/unlit.vert.glsl",
                "default_unlit/unlit.frag.glsl",
            },
            .pipeline_settings = rendergraph::RasterizationSettings{
                .depth_test = true,
                .depth_write = false,
                .depth_op = VK_COMPARE_OP_LESS_OR_EQUAL,
            },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                auto& r = get_renderer();
                auto r_col_att_1 = Vks(VkRenderingAttachmentInfo{
                    .imageView = r.get_image(r.get_frame_data().gbuffer.color_image).view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
                });

                VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };
                auto r_dep_att = Vks(VkRenderingAttachmentInfo{
                    .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_NONE,
                    .clearValue = { .depthStencil = { 1.0f, 0 } },
                });

                auto rendering_info = Vks(VkRenderingInfo{
                    .renderArea = { .extent = { .width = (uint32_t)r.screen_rect.w, .height = (uint32_t)r.screen_rect.h } },
                    .layerCount = 1,
                    .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
                    .pColorAttachments = r_col_atts,
                    .pDepthAttachment = &r_dep_att,
                });

                vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd,pass.pipeline_bind_point, r.bindless_layout.layout, 0, 1, &r.bindless_set, 0, nullptr);
                vkCmdBeginRendering(cmd, &rendering_info);
                VkRect2D r_sciss_1{ .offset = {}, .extent = { (uint32_t)r.screen_rect.w, (uint32_t)r.screen_rect.h } };
                VkViewport r_view_1{
                    .x = 0.0f, .y = r.screen_rect.h, .width = r.screen_rect.w, .height = -r.screen_rect.h, .minDepth = 0.0f, .maxDepth = 1.0f
                };
                vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
                vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
                uint32_t bindless_indices[]{
                    r.get_bindless_index(r.index_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vertex_positions_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vertex_attributes_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.get_frame_data().constants, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.mesh_instances_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.transform_buffers[0], BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vsm_data_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vsm_shadow_map_0, BindlessType::STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL, nullptr),
                    0,
                };
                vkCmdPushConstants(cmd, r.bindless_layout.layout, VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
                vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer, sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer, 0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
                vkCmdEndRendering(cmd);
        } })
        .add_pass(rendergraph::RenderPass{
            .accesses = { 
                rendergraph::Access{
                    { *fd.gbuffer.depth_buffer_image, rendergraph::ResourceType::COLOR_ATTACHMENT },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
                },
                rendergraph::Access{
                    { *get_renderer().vsm_dir_light_page_table, rendergraph::ResourceType::COLOR_ATTACHMENT },
                    rendergraph::AccessType::READ_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL
                },
                rendergraph::Access{
                    { *get_renderer().vsm_free_allocs_buffer, rendergraph::ResourceType::STORAGE_BUFFER },
                    rendergraph::AccessType::READ_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT
                },
            },
            .shaders = { "vsm/page_alloc.comp.glsl" },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                auto& r = get_renderer();
                uint32_t bindless_indices[]{
                    r.get_bindless_index(r.get_frame_data().gbuffer.depth_buffer_image, BindlessType::COMBINED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, r.samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)),
                    r.get_bindless_index(r.vsm_dir_light_page_table, BindlessType::STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL, nullptr),
                    r.get_bindless_index(r.get_frame_data().constants, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vsm_data_buffer, BindlessType::STORAGE_BUFFER),
                };
                vkCmdPushConstants(cmd, r.bindless_layout.layout, VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
                vkCmdDispatch(cmd, (uint32_t)std::ceilf(r.screen_rect.w / 8.0f), (uint32_t)std::ceilf(r.screen_rect.h / 8.0f), 1);
            }
        })
       
       /* .add_pass(rendergraph::RenderPass{ 
            .accesses = {
                rendergraph::Access{
                    { *fd.gbuffer.color_image, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::WRITE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                },
                rendergraph::Access{
                    { *fd.gbuffer.depth_buffer_image, rendergraph::ResourceType::COLOR_ATTACHMENT },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                },
                rendergraph::Access{
                    { *get_renderer().vsm_shadow_map_0, rendergraph::ResourceType::STORAGE_IMAGE },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,
                },
            },
            .shaders = {
                "default_unlit/unlit.vert.glsl",
                "default_unlit/unlit.frag.glsl",
            },
            .pipeline_settings = rendergraph::RasterizationSettings{
                .depth_test = true,
                .depth_write = false,
                .depth_op = VK_COMPARE_OP_LESS_OR_EQUAL,
            },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                auto& r = get_renderer();
                auto r_col_att_1 = Vks(VkRenderingAttachmentInfo{
                    .imageView = r.get_image(r.get_frame_data().gbuffer.color_image).view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
                });

                VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };
                auto r_dep_att = Vks(VkRenderingAttachmentInfo{
                    .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).view,
                    .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_NONE,
                    .clearValue = { .depthStencil = { 1.0f, 0 } },
                });

                auto rendering_info = Vks(VkRenderingInfo{
                    .renderArea = { .extent = { .width = (uint32_t)r.screen_rect.w, .height = (uint32_t)r.screen_rect.h } },
                    .layerCount = 1,
                    .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
                    .pColorAttachments = r_col_atts,
                    .pDepthAttachment = &r_dep_att,
                });

                vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(cmd,pass.pipeline_bind_point, r.bindless_layout.layout, 0, 1, &r.bindless_set, 0, nullptr);
                vkCmdBeginRendering(cmd, &rendering_info);
                VkRect2D r_sciss_1{ .offset = {}, .extent = { (uint32_t)r.screen_rect.w, (uint32_t)r.screen_rect.h } };
                VkViewport r_view_1{
                    .x = 0.0f, .y = r.screen_rect.h, .width = r.screen_rect.w, .height = -r.screen_rect.h, .minDepth = 0.0f, .maxDepth = 1.0f
                };
                vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
                vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
                uint32_t bindless_indices[]{
                    r.get_bindless_index(r.index_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vertex_positions_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vertex_attributes_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.get_frame_data().constants, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.mesh_instances_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.transform_buffers[0], BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vsm_data_buffer, BindlessType::STORAGE_BUFFER),
                    r.get_bindless_index(r.vsm_shadow_map_0, BindlessType::STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL, nullptr),
                    1,
                };
                vkCmdPushConstants(cmd, r.bindless_layout.layout, VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
                vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer, sizeof(IndirectDrawCommandBufferHeader), r.get_buffer(r.indirect_draw_buffer).buffer, 0ull, r.max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
                vkCmdEndRendering(cmd);
            } })*/
        .add_pass(rendergraph::RenderPass{
            .accesses = {
                rendergraph::Access{
                    { *get_renderer().vsm_dir_light_page_table, rendergraph::ResourceType::COLOR_ATTACHMENT },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,
                },
                rendergraph::Access{
                    { *get_renderer().vsm_dir_light_page_table_rgb8, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,
                },
            },
            .shaders = { "vsm/debug_page_alloc_copy.comp.glsl" },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                auto& r = get_renderer();
                uint32_t bindless_indices[]{
                    r.get_bindless_index(r.vsm_dir_light_page_table, BindlessType::STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL, nullptr),
                    r.get_bindless_index(r.vsm_dir_light_page_table_rgb8, BindlessType::STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL, nullptr),
                };
                vkCmdBindDescriptorSets(cmd,pass.pipeline_bind_point, r.bindless_layout.layout, 0, 1, &r.bindless_set, 0, nullptr);
                vkCmdPushConstants(cmd, r.bindless_layout.layout, VK_SHADER_STAGE_ALL, 0, sizeof(bindless_indices), bindless_indices);
                vkCmdDispatch(cmd, 64/8, 64/8, 1);
            },
         })
        .add_pass(rendergraph::RenderPass{
            .accesses = {
                rendergraph::Access{
                    {rendergraph::swapchain_index, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::SWAPCHAIN_IMAGE_BIT | rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                },
                rendergraph::Access{
                    { *fd.gbuffer.color_image, rendergraph::ResourceType::COLOR_ATTACHMENT },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                },
                rendergraph::Access{
                    { *get_renderer().vsm_dir_light_page_table_rgb8, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT },
                    rendergraph::AccessType::READ_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                },
            },
            .callback_render = [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass&) {
                    auto& r = get_renderer();
                    ImGui::SetCurrentContext(Engine::get().ui.context->imgui_ctx);
                        ImGui_ImplVulkan_NewFrame();
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                    ImGuizmo::BeginFrame();
                    Engine::get().ui.update();
                    ImGui::Render();
                    ImDrawData* im_draw_data = ImGui::GetDrawData();
                    if(im_draw_data) {
                        VkRenderingAttachmentInfo r_col_atts[]{
                            Vks(VkRenderingAttachmentInfo{
                                .imageView = get_renderer().swapchain.images.at(swapchain_index).view,
                                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
                            }),
                        };
                        VkRect2D r_sciss_1{ .offset = {}, .extent = { (uint32_t)get_renderer().screen_rect.w, (uint32_t)get_renderer().screen_rect.h } };
                        VkViewport r_view_1{
                            .x = 0.0f, .y = get_renderer().screen_rect.h, .width = get_renderer().screen_rect.w, .height = -get_renderer().screen_rect.h, .minDepth = 0.0f, .maxDepth = 1.0f
                        };
                        auto rendering_info = Vks(VkRenderingInfo{
                            .renderArea = { .extent = { .width = (uint32_t)get_renderer().screen_rect.w, .height = (uint32_t)get_renderer().screen_rect.h } },
                            .layerCount = 1,
                            .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
                            .pColorAttachments = r_col_atts,
                        });
                        /*swapchain_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);*/
                        vkCmdBeginRendering(cmd, &rendering_info);
                        vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
                        vkCmdSetViewport(cmd, 0, 1, &r_view_1);
                        ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
                        vkCmdEndRendering(cmd);
                    }
            },
        })
        .add_pass(rendergraph::RenderPass{
            .accesses = {
                rendergraph::Access{
                    {rendergraph::swapchain_index, rendergraph::ResourceType::COLOR_ATTACHMENT, rendergraph::ResourceFlags::SWAPCHAIN_IMAGE_BIT  },
                    rendergraph::AccessType::NONE_BIT,
                    VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                },
            }
        })
        .bake();
    }
}

void RendererVulkan::update() {
    if(screen_rect.w * screen_rect.h == 0.0f) { return; }
    if(flags.test_clear(RenderFlags::DIRTY_GEOMETRY_BATCHES_BIT)) { upload_staged_models(); }
    if(flags.test_clear(RenderFlags::DIRTY_MESH_INSTANCES)) {
        bake_indirect_commands();
        upload_transforms();
    }
    if(flags.test_clear(RenderFlags::DIRTY_BLAS_BIT)) { build_blas(); }
    if(flags.test_clear(RenderFlags::DIRTY_TLAS_BIT)) {
        build_tlas();
        update_ddgi();
        // TODO: prepare ddgi on scene update
    }
    if(flags.test_clear(RenderFlags::RESIZE_SWAPCHAIN_BIT)) {
        gq.wait_idle();
        create_window_sized_resources();
        build_render_graph();
    }
    if(flags.test_clear(RenderFlags::UPDATE_BINDLESS_SET)) {
        gq.wait_idle();
        update_bindless_set();
    }

    auto& fd = get_frame_data();
    const auto frame_num = Engine::get().frame_num();
    fd.fen_rendering_finished.wait();
    fd.cmdpool->reset();

    uint32_t swapchain_index{};
    Image* swapchain_image{};
    {
        VkResult acquire_ret;
        swapchain_index = swapchain.acquire(&acquire_ret, ~0ull, fd.sem_swapchain.semaphore);
        if(acquire_ret != VK_SUCCESS) {
            ENG_WARN("Acquire image failed with: {}", static_cast<uint32_t>(acquire_ret));
            return;
        }
        swapchain_image = &swapchain.images[swapchain_index];
    }

    vkResetFences(dev, 1, &get_frame_data().fen_rendering_finished.fence);

    {
        const float hx = (halton(Engine::get().frame_num() % 4u, 2) * 2.0 - 1.0);
        const float hy = (halton(Engine::get().frame_num() % 4u, 3) * 2.0 - 1.0);
        const glm::mat3 rand_mat =
            glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

        const auto ldir = glm::normalize(*(glm::vec3*)Engine::get().scene->debug_dir_light_dir);
        const auto eye = -ldir * 25.0f;
        auto vsm_light_mat = glm::lookAt(eye, ldir, glm::vec3{ 0.0f, 1.0f, 0.0f });
        const auto camdir = Engine::get().camera->pos - eye;
        const auto d = glm::dot(ldir, camdir);
        auto proj_pos = -glm::vec4{ camdir - d * ldir, 0.0f };
        proj_pos = vsm_light_mat * proj_pos;

        GPUVsmBuffer vsmconsts{
            .dir_light_view = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ glm::vec2{ proj_pos }, 0.0f }) * vsm_light_mat,
            .dir_light_proj = glm::ortho(-35.0f, 35.0f, -35.0f, 35.0f, 0.1f, 50.0f),
            .num_frags = 0,
        };

        GPUConstants constants{
            .view = Engine::get().camera->get_view(),
            .proj = Engine::get().camera->get_projection(),
            .inv_view = glm::inverse(Engine::get().camera->get_view()),
            .inv_proj = glm::inverse(Engine::get().camera->get_projection()),
        };
        send_many(fd.constants, 0ull, constants);
        send_to(vsm_data_buffer, 0ull, &vsmconsts, sizeof(glm::mat4) * 2);
        send_to(vsm_data_buffer, offsetof(GPUVsmBuffer, num_frags), &vsmconsts.num_frags, 4);
    }

    auto cmd = fd.cmdpool->begin_onetime();

    if(flags.test_clear(RenderFlags::DIRTY_TRANSFORMS_BIT)) {
        std::swap(transform_buffers[0], transform_buffers[1]);
        Buffer& dst_transforms = get_buffer(transform_buffers[0]);
        std::vector<glm::mat4> transforms;
        transforms.reserve(mesh_instances.size());
        for(auto e : mesh_instances) {
            transforms.push_back(Engine::get().ecs_storage->get<components::Transform>(e).transform);
        }
        send_to(transform_buffers[0], 0ull, std::as_bytes(std::span{ transforms }));
        auto barr = Vks(VkBufferMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                .srcAccessMask = VK_ACCESS_NONE,
                                                .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                                .dstAccessMask = VK_ACCESS_NONE,
                                                .buffer = get_buffer(transform_buffers[0]).buffer,
                                                .size = VK_WHOLE_SIZE });
        auto info = Vks(VkDependencyInfo{ .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barr });
        vkCmdPipelineBarrier2(cmd, &info);
        update_positions.clear();
    }

    auto barr = Vks(VkBufferMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_HOST_BIT,
                                            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
                                            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                                            .buffer = get_buffer(vsm_data_buffer).buffer,
                                            .size = VK_WHOLE_SIZE });
    auto info = Vks(VkDependencyInfo{ .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barr });
    vkCmdPipelineBarrier2(cmd, &info);

    fd.render_graph.render(cmd, swapchain_index);
    barr = Vks(VkBufferMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                       .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                       .dstStageMask = VK_PIPELINE_STAGE_HOST_BIT,
                                       .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
                                       .buffer = get_buffer(vsm_data_buffer).buffer,
                                       .size = VK_WHOLE_SIZE });
    vkCmdPipelineBarrier2(cmd, &info);

    fd.cmdpool->end(cmd);
    gq.submit(QueueSubmission{ .cmds = { cmd },
                               .wait_sems = { { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, fd.sem_swapchain } },
                               .signal_sems = { { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, fd.sem_rendering_finished } } },
              &fd.fen_rendering_finished);

    auto pinfo = Vks(VkPresentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &fd.sem_rendering_finished.semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain.swapchain,
        .pImageIndices = &swapchain_index,
    });
    vkQueuePresentKHR(gq.queue, &pinfo);
    if(!flags.empty()) { ENG_WARN("render flags not empty at the end of the frame: {:b}", flags.flags); }

    // flags.clear();
    gq.wait_idle();
    return;
    const auto num_frags = ((GPUVsmBuffer*)get_buffer(vsm_data_buffer).memory)->num_frags;
    auto* pages = &((GPUVsmBuffer*)get_buffer(vsm_data_buffer).memory)->pages;
    std::sort(pages, pages + num_frags);
    auto pages_end = std::unique(pages, pages + num_frags);
    ENG_LOG("num pages: {}", num_frags);
    for(auto i = pages; i != pages_end; ++i) {
        std::print("{} ", *i);
    }
    std::print("\n");
}

void RendererVulkan::on_window_resize() {
    flags.set(RenderFlags::RESIZE_SWAPCHAIN_BIT);
    set_screen(ScreenRect{ .w = Engine::get().window->width, .h = Engine::get().window->height });
}

void RendererVulkan::set_screen(ScreenRect screen) {
    screen_rect = screen;
    // ENG_WARN("TODO: Resize resources on new set_screen()");
}

static VkFormat deduce_image_format(ImageFormat format) {
    switch(format) {
    case ImageFormat::UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::SRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    default: {
        assert(false);
        return VK_FORMAT_MAX_ENUM;
    }
    }
}

static VkImageType deduce_image_type(ImageType dim) {
    switch(dim) {
    case ImageType::DIM_1D:
        return VK_IMAGE_TYPE_1D;
    case ImageType::DIM_2D:
        return VK_IMAGE_TYPE_2D;
    case ImageType::DIM_3D:
        return VK_IMAGE_TYPE_3D;
    default: {
        assert(false);
        return VK_IMAGE_TYPE_MAX_ENUM;
    }
    }
}

Handle<Image> RendererVulkan::batch_texture(const ImageDescriptor& desc) {
    const auto handle = make_image(desc.name, deduce_image_format(desc.format), deduce_image_type(desc.type),
                                   VkExtent3D{ .width = desc.width, .height = desc.height, .depth = 1u }, desc.mips, 1u,
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_images.push_back(UploadImage{ handle, { desc.data.begin(), desc.data.end() } });
    return handle;
}

Handle<RenderMaterial> RendererVulkan::batch_material(const MaterialDescriptor& desc) {
    return Handle<RenderMaterial>{ *materials.insert(RenderMaterial{ .textures = desc }) };
}

Handle<RenderGeometry> RendererVulkan::batch_geometry(const GeometryDescriptor& batch) {
    const auto total_vertices = get_total_vertices();
    const auto total_indices = get_total_indices();

    RenderGeometry geometry{ .metadata = geometry_metadatas.emplace(),
                             .vertex_offset = total_vertices,
                             .vertex_count = (uint32_t)batch.vertices.size(),
                             .index_offset = total_indices,
                             .index_count = (uint32_t)batch.indices.size() };

    upload_vertices.insert(upload_vertices.end(), batch.vertices.begin(), batch.vertices.end());
    upload_indices.insert(upload_indices.end(), batch.indices.begin(), batch.indices.end());

    Handle<RenderGeometry> handle = geometries.insert(geometry);

    flags.set(RenderFlags::DIRTY_GEOMETRY_BATCHES_BIT);

    // clang-format off
    ENG_LOG("Batching geometry: [VXS: {:.2f} KB, IXS: {:.2f} KB]", 
            static_cast<float>(batch.vertices.size_bytes()) / 1000.0f,
            static_cast<float>(batch.indices.size_bytes()) / 1000.0f);
    // clang-format on

    return handle;
}

Handle<RenderMesh> RendererVulkan::batch_mesh(const MeshDescriptor& batch) {
    RenderMesh mesh_batch{
        .geometry = batch.geometry,
        .metadata = mesh_metadatas.emplace(),
    };
    return meshes.insert(mesh_batch);
}

void RendererVulkan::instance_mesh(const InstanceSettings& settings) {
    mesh_instances.push_back(settings.entity);
    flags.set(RenderFlags::DIRTY_MESH_INSTANCES);
}

void RendererVulkan::instance_blas(const BLASInstanceSettings& settings) {
    auto& r = Engine::get().ecs_storage->get<components::Renderable>(settings.entity);
    auto& mesh = meshes.at(r.mesh_handle);
    auto& geometry = geometries.at(mesh.geometry);
    auto& metadata = geometry_metadatas.at(geometry.metadata);
    blas_instances.push_back(settings.entity);
    flags.set(RenderFlags::DIRTY_TLAS_BIT);
    if(!metadata.blas) {
        geometry.flags.set(GeometryFlags::DIRTY_BLAS_BIT);
        flags.set(RenderFlags::DIRTY_BLAS_BIT);
    }
}

void RendererVulkan::update_transform(components::Entity entity) {
    update_positions.push_back(entity);
    flags.set(RenderFlags::DIRTY_TRANSFORMS_BIT);
}

size_t RendererVulkan::get_imgui_texture_id(Handle<Image> handle, ImageFilter filter, ImageAddressing addressing) {
    struct ImguiTextureId {
        ImTextureID id;
        VkImage image;
        ImageFilter filter;
        ImageAddressing addressing;
    };
    static std::unordered_multimap<Handle<Image>, ImguiTextureId> tex_ids;
    auto range = tex_ids.equal_range(handle);
    auto delete_it = tex_ids.end();
    for(auto it = range.first; it != range.second; ++it) {
        if(it->second.filter == filter && it->second.addressing == addressing) {
            if(it->second.image != get_image(handle).image) {
                ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(it->second.id));
                delete_it = it;
                break;
            }
            return it->second.id;
        }
    }
    if(delete_it != tex_ids.end()) { tex_ids.erase(delete_it); }
    ImguiTextureId id{
        .id = reinterpret_cast<ImTextureID>(ImGui_ImplVulkan_AddTexture(samplers.get_sampler(filter, addressing),
                                                                        get_image(handle).view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL)),
        .image = get_image(handle).image,
        .filter = filter,
        .addressing = addressing,
    };
    tex_ids.emplace(handle, id);
    return id.id;
}

RenderMaterial RendererVulkan::get_material(Handle<RenderMaterial> handle) const {
    if(!handle) { return RenderMaterial{}; }
    return materials.at(handle);
}

void RendererVulkan::upload_model_textures() {
    staging_buffer->begin();
    for(auto& tex : upload_images) {
        Image& img = get_image(tex.image_handle);
        img.current_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        VkBufferImageCopy copy{
            .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
            .imageOffset = {},
            .imageExtent = img.extent,
        };
        staging_buffer->send(img, std::as_bytes(std::span{ tex.rgba_data }), copy);
    }
    staging_buffer->stage();
    upload_images.clear();
}

void RendererVulkan::upload_staged_models() {
    upload_model_textures();
    std::vector<glm::vec3> positions;
    std::vector<float> attributes;
    positions.reserve(upload_vertices.size());
    attributes.reserve(upload_vertices.size() * 8);
    for(auto& e : upload_vertices) {
        positions.push_back(e.pos);
        attributes.push_back(e.nor.x);
        attributes.push_back(e.nor.y);
        attributes.push_back(e.nor.z);
        attributes.push_back(e.uv.x);
        attributes.push_back(e.uv.y);
        attributes.push_back(e.tang.x);
        attributes.push_back(e.tang.y);
        attributes.push_back(e.tang.z);
        attributes.push_back(e.tang.w);
    }
    send_to(vertex_positions_buffer, ~0ull, std::as_bytes(std::span{ positions }));
    send_to(vertex_attributes_buffer, ~0ull, std::as_bytes(std::span{ attributes }));
    send_to(index_buffer, ~0ull, std::as_bytes(std::span{ upload_indices }));
    upload_vertices.clear();
    upload_indices.clear();
}

void RendererVulkan::bake_indirect_commands() {
    std::sort(mesh_instances.begin(), mesh_instances.end(), [](auto a, auto b) {
        const auto& ra = Engine::get().ecs_storage->get<components::Renderable>(a);
        const auto& rb = Engine::get().ecs_storage->get<components::Renderable>(b);
        if(ra.material_handle >= rb.material_handle) { return false; }
        if(ra.mesh_handle >= rb.mesh_handle) { return false; }
        return true;
    });

    mesh_instance_idxs.clear();
    mesh_instance_idxs.reserve(mesh_instances.size());
    for(uint32_t i = 0; i < mesh_instances.size(); ++i) {
        mesh_instance_idxs[mesh_instances.at(i)] = i;
    }

    const auto total_triangles = get_total_triangles();
    std::vector<GPUMeshInstance> gpu_mesh_instances;
    std::vector<VkDrawIndexedIndirectCommand> gpu_draw_commands;
    IndirectDrawCommandBufferHeader gpu_draw_header;

    for(uint32_t i = 0u; i < mesh_instances.size(); ++i) {
        const components::Renderable& mi = Engine::get().ecs_storage->get<components::Renderable>(mesh_instances.at(i));
        const RenderMesh& mb = meshes.at(mi.mesh_handle);
        const RenderGeometry& geom = geometries.at(mb.geometry);
        const RenderMaterial& mat = materials.at(mi.material_handle);
        gpu_mesh_instances.push_back(GPUMeshInstance{
            .vertex_offset = geom.vertex_offset,
            .index_offset = geom.index_offset,
            .color_texture_idx = get_bindless_index(mat.textures.base_color_texture.handle, BindlessType::COMBINED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                    samplers.get_sampler(mat.textures.base_color_texture.filter,
                                                                         mat.textures.base_color_texture.addressing)),
            .normal_texture_idx =
                get_bindless_index(mat.textures.normal_texture.handle, BindlessType::COMBINED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                   samplers.get_sampler(mat.textures.normal_texture.filter, mat.textures.normal_texture.addressing)),
            .metallic_roughness_idx = get_bindless_index(mat.textures.metallic_roughness_texture.handle,
                                                         BindlessType::COMBINED_IMAGE, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                         samplers.get_sampler(mat.textures.metallic_roughness_texture.filter,
                                                                              mat.textures.metallic_roughness_texture.addressing)),
        });
        if(i == 0 || Engine::get().ecs_storage->get<components::Renderable>(mesh_instances.at(i - 1)).mesh_handle != mi.mesh_handle) {
            gpu_draw_commands.push_back(VkDrawIndexedIndirectCommand{ .indexCount = geom.index_count,
                                                                      .instanceCount = 1,
                                                                      .firstIndex = geom.index_offset,
                                                                      .vertexOffset = (int32_t)geom.vertex_offset, /*(int32_t)(geom.vertex_offset),*/
                                                                      .firstInstance = i });
        } else {
            ++gpu_draw_commands.back().instanceCount;
        }
    }

    gpu_draw_header.draw_count = gpu_draw_commands.size();
    gpu_draw_header.geometry_instance_count = mesh_instances.size();
    max_draw_count = gpu_draw_commands.size();

    // clang-format off
    if(!indirect_draw_buffer){
        indirect_draw_buffer = make_buffer("indirect draw", sizeof(IndirectDrawCommandBufferHeader) + gpu_draw_commands.size() * sizeof(gpu_draw_commands[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    }
    send_to(indirect_draw_buffer, 0ull, &gpu_draw_header, sizeof(gpu_draw_header));
    send_to(indirect_draw_buffer, ~0ull, std::as_bytes(std::span{gpu_draw_commands}));

    if(!mesh_instances_buffer) {
        mesh_instances_buffer = make_buffer("mesh instances", gpu_mesh_instances.size() * sizeof(gpu_mesh_instances[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    }
    send_to(mesh_instances_buffer, 0ull, std::as_bytes(std::span{gpu_mesh_instances}));
    // clang-format on
}

void RendererVulkan::upload_transforms() {
    std::swap(transform_buffers[0], transform_buffers[1]);
    Buffer& dst_transforms = get_buffer(transform_buffers[0]);
    std::vector<glm::mat4> transforms;
    transforms.reserve(mesh_instances.size());
    for(auto e : mesh_instances) {
        transforms.push_back(Engine::get().ecs_storage->get<components::Transform>(e).transform);
    }
    send_to(transform_buffers[0], 0ull, std::as_bytes(std::span{ transforms }));
}

void RendererVulkan::update_bindless_set() {
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> buffer_writes;
    std::vector<VkDescriptorImageInfo> image_writes;
    writes.reserve(bindless_resources_to_update.size());
    buffer_writes.reserve(bindless_resources_to_update.size());
    image_writes.reserve(bindless_resources_to_update.size());
    for(const auto& e : bindless_resources_to_update) {
        auto& write = writes.emplace_back(Vks(VkWriteDescriptorSet{
            .dstSet = bindless_set,
            .dstBinding = e.type == BindlessType::STORAGE_BUFFER   ? BINDLESS_STORAGE_BUFFER_BINDING
                          : e.type == BindlessType::STORAGE_IMAGE  ? BINDLESS_STORAGE_IMAGE_BINDING
                          : e.type == BindlessType::COMBINED_IMAGE ? BINDLESS_COMBINED_IMAGE_BINDING
                                                                   : ~0ul,
            .dstArrayElement = bindless.indices.at(e),
            .descriptorCount = 1,
            .descriptorType = e.to_vk_descriptor_type(),
        }));
        if(e.type == BindlessType::STORAGE_BUFFER) {
            write.pBufferInfo = &buffer_writes.emplace_back(VkDescriptorBufferInfo{
                .buffer = buffers.at(e.resource_handle).buffer, .offset = 0, .range = VK_WHOLE_SIZE });
        } else if(e.type == BindlessType::STORAGE_IMAGE || e.type == BindlessType::COMBINED_IMAGE) {
            write.pImageInfo = &image_writes.emplace_back(VkDescriptorImageInfo{
                .sampler = e.sampler,
                .imageView = images.at(e.resource_handle).view,
                .imageLayout = e.layout,
            });
        }
    }
    vkUpdateDescriptorSets(dev, writes.size(), writes.data(), 0, nullptr);
}

void RendererVulkan::build_blas() {
    ENG_TODO("IMPLEMENT BACK");
    return;
    auto triangles = Vks(VkAccelerationStructureGeometryTrianglesDataKHR{
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = { .deviceAddress = get_buffer(vertex_positions_buffer).bda },
        .vertexStride = sizeof(glm::vec3),
        .maxVertex = get_total_vertices() - 1u,
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData = { .deviceAddress = get_buffer(index_buffer).bda },
    });

    std::vector<const RenderGeometry*> dirty_batches;
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
        scratch_sizes.push_back(align_up(build_size_info.buildScratchSize,
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
        const RenderGeometry& geom = *dirty_batches.at(i);
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
    Fence f{ dev, false };
    gq.submit(cmd, &f);
    f.wait();
}

void RendererVulkan::build_tlas() {
    return;
    std::vector<uint32_t> tlas_mesh_offsets;
    std::vector<uint32_t> blas_mesh_offsets;
    std::vector<uint32_t> triangle_geo_inst_ids;
    std::vector<VkAccelerationStructureInstanceKHR> tlas_instances;

    std::sort(blas_instances.begin(), blas_instances.end(), [](auto a, auto b) {
        const auto& ra = Engine::get().ecs_storage->get<components::Renderable>(a);
        const auto& rb = Engine::get().ecs_storage->get<components::Renderable>(b);
        return ra.mesh_handle < rb.mesh_handle;
    });

    assert(false);

// TODO: Compress mesh ids per triangle for identical blases with identical materials
// TODO: Remove geometry offset for indexing in shaders as all blases have only one geometry always
#if 0
    for(uint32_t i = 0, toff = 0, boff = 0; i < blas_instances.size(); ++i) {
        const uint32_t mi_idx = mesh_instance_idxs.at(blas_instances.at(i));
        const auto& mr = Engine::get().ecs_storage->get<components::Renderable>(mesh_instances.at(mi_idx));
        const RenderMesh& mb = meshes.at(mr.mesh_handle);
        const RenderGeometry& geom = geometries.at(mb.geometry);
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
#endif
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
}

void RendererVulkan::update_ddgi() {
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
    gq.wait_idle();
#endif
}

Image RendererVulkan::allocate_image(const std::string& name, VkFormat format, VkImageType type, VkExtent3D extent,
                                     uint32_t mips, uint32_t layers, VkImageUsageFlags usage) {
    const auto info = Vks(VkImageCreateInfo{
        .imageType = type,
        .format = format,
        .extent =
            VkExtent3D{
                .width = std::max(extent.width, 1u),
                .height = std::max(extent.height, 1u),
                .depth = std::max(extent.depth, 1u),
            },
        .mipLevels = mips,
        .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = usage,
    });

    Image img{ .format = format, .usage = usage, .extent = info.extent, .mips = mips, .layers = layers };
    VmaAllocationCreateInfo vma_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    VK_CHECK(vmaCreateImage(vma, &info, &vma_info, &img.image, &img.alloc, nullptr));
    img._deduce_aspect(img.usage);
    img._create_default_view(type == VK_IMAGE_TYPE_3D   ? 3
                             : type == VK_IMAGE_TYPE_2D ? 2
                             : type == VK_IMAGE_TYPE_1D ? 1
                                                        : 0);
    set_debug_name(img.image, std::format("image_{}", name));
    set_debug_name(img.view, std::format("image_{}_default_view", name));
    return img;
}

Handle<Image> RendererVulkan::make_image(const std::string& name, VkFormat format, VkImageType type, VkExtent3D extent,
                                         uint32_t mips, uint32_t layers, VkImageUsageFlags usage) {
    const auto handle = Handle<Image>{ (uint32_t)images.size() };
    images.push_back(allocate_image(name, format, type, extent, mips, layers, usage));
    return handle;
}

Image& RendererVulkan::get_image(Handle<Image> handle) { return images.at(*handle); }

uint32_t RendererVulkan::get_bindless_index(Handle<Image> handle, BindlessType type, VkImageLayout layout, VkSampler sampler) {
    if(!handle) { return *handle; }
    const BindlessEntry entry{ .resource_handle = *handle, .type = type, .layout = layout, .sampler = sampler };
    if(auto it = bindless.indices.find(entry); it != bindless.indices.end()) { return it->second; }
    const auto index = bindless.resource_indices_arr[std::to_underlying(type) - 1]++;
    bindless.indices[entry] = index;
    auto cached_it = std::lower_bound(bindless.cached_resources.begin(), bindless.cached_resources.end(), entry,
                                      [](auto& a, auto& b) { return a.resource_handle < b.resource_handle; });
    bindless.cached_resources.insert(cached_it, entry);
    update_bindless_resource(handle);
    return index;
}

uint32_t RendererVulkan::get_bindless_index(Handle<Buffer> handle, BindlessType type) {
    return get_bindless_index(Handle<Image>{ *handle }, type, VK_IMAGE_LAYOUT_UNDEFINED, nullptr);
}

void RendererVulkan::update_bindless_resource(Handle<Image> handle) {
    auto cached_it = std::lower_bound(bindless.cached_resources.begin(), bindless.cached_resources.end(), *handle,
                                      [](auto& a, auto& b) { return a.resource_handle < b; });
    for(auto it = cached_it; it != bindless.cached_resources.end() && it->resource_handle == *handle; ++it) {
        bindless_resources_to_update.push_back(*it);
    }
    flags.set(RenderFlags::UPDATE_BINDLESS_SET);
}

void RendererVulkan::update_bindless_resource(Handle<Buffer> handle) {
    update_bindless_resource(Handle<Image>{ *handle });
}

void RendererVulkan::destroy_image(const Image** img) {
    assert(false);
    /*if(img && *img) {
        std::erase_if(images, [&img](const auto& e) { return e.image == (*img)->image; });
        *img = nullptr;
    } else {
        ENG_WARN("Trying to destroy nullptr image.");
    }*/
}

Handle<Buffer> RendererVulkan::make_buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map, uint32_t alignment) {
    auto handle = Handle<Buffer>{ static_cast<Handle<Buffer>::Storage_T>(buffers.size()) };
    buffers.push_back(allocate_buffer(name, size, usage, map, alignment));
    return handle;
}

Buffer RendererVulkan::allocate_buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map, uint32_t alignment) {
    size = std::max(size, 128ull);
    if(!map) { usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT; }
    Buffer buffer{ .name = name, .capacity = size, .alignment = alignment, .usage = usage };
    const auto info = Vks(VkBufferCreateInfo{ .size = size, .usage = usage });
    VmaAllocationCreateInfo alloc_info{ .usage = VMA_MEMORY_USAGE_AUTO, .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT };
    if(map) {
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    VK_CHECK(vmaCreateBufferWithAlignment(vma, &info, &alloc_info, alignment, &buffer.buffer, &buffer.allocation, nullptr));
    if(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        const auto info = Vks(VkBufferDeviceAddressInfo{ .buffer = buffer.buffer });
        buffer.bda = vkGetBufferDeviceAddress(dev, &info);
    }
    if(alloc_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
        VmaAllocationInfo allocation{};
        vmaGetAllocationInfo(vma, buffer.allocation, &allocation);
        buffer.memory = allocation.pMappedData;
    }
    set_debug_name(buffer.buffer, name);
    ENG_LOG("ALLOCATING BUFFER {} OF SIZE {:.2f} KB", name.c_str(), static_cast<float>(size) / 1024.0f);
    return buffer;
}

void RendererVulkan::deallocate_buffer(Buffer& buffer) {
    vmaDestroyBuffer(vma, buffer.buffer, buffer.allocation);
    buffer.buffer = nullptr;
    buffer.allocation = nullptr;
    buffer.memory = nullptr;
    buffer.bda = 0ull;
}

void RendererVulkan::destroy_buffer(Handle<Buffer> handle) { assert(false); }

void RendererVulkan::resize_buffer(Handle<Buffer> handle, size_t new_size) {
    auto& old_buffer = get_buffer(handle);
    if(new_size <= old_buffer.capacity) {
        old_buffer.size = new_size;
        return;
    }
    auto new_buffer = allocate_buffer(old_buffer.name, new_size, old_buffer.usage, !!old_buffer.memory, old_buffer.alignment);
    if(old_buffer.size > 0ull) { staging_buffer->send(new_buffer, 0ull, old_buffer, 0ull, old_buffer.size); }
    new_buffer.size = old_buffer.size;
    deallocate_buffer(old_buffer);
    old_buffer = new_buffer;
    update_bindless_resource(handle);
}

Buffer& RendererVulkan::get_buffer(Handle<Buffer> handle) { return buffers.at(*handle); }

void RendererVulkan::send_to(Handle<Buffer> dst, size_t dst_offset, Handle<Buffer> src, size_t src_offset, size_t size) {
    assert(dst && src);
    auto& dstb = get_buffer(dst);
    auto& srcb = get_buffer(src);
    dst_offset = (dst_offset == ~0ull) ? dstb.size : dst_offset;
    const auto total_size = dst_offset + size;
    if(dstb.capacity < total_size) { resize_buffer(dst, total_size); }
    assert(dst_offset + size <= dstb.capacity && src_offset + size <= srcb.size);
    staging_buffer->send(dstb, dst_offset, srcb, src_offset, size);
    dstb.size = total_size;
}

void RendererVulkan::send_to(Handle<Buffer> dst, size_t dst_offset, void* src, size_t size) {
    dst_offset = (dst_offset == ~0ull) ? get_buffer(dst).size : dst_offset;
    const auto total_size = dst_offset + size;
    if(get_buffer(dst).capacity < total_size) { resize_buffer(dst, total_size); }
    if(get_buffer(dst).memory) {
        memcpy((std::byte*)get_buffer(dst).memory + dst_offset, src, size);
    } else {
        staging_buffer->send(get_buffer(dst), dst_offset, std::span{ (std::byte*)src, size });
    }
    get_buffer(dst).size = total_size;
}

template <typename... Ts> void RendererVulkan::send_many(Handle<Buffer> dst, size_t dst_offset, const Ts&... ts) {
    std::array<std::byte, (sizeof(Ts) + ... + 0)> arr;
    size_t offset = 0;
    (..., [&arr, &offset](const auto& t) {
        memcpy(&arr[offset], &t, sizeof(decltype(t)));
        offset += sizeof(decltype(t));
    }(ts));
    send_to(dst, dst_offset, (void*)arr.data(), arr.size());
}

FrameData& RendererVulkan::get_frame_data(uint32_t offset) {
    return frame_datas[(Engine::get().frame_num() + offset) % frame_datas.size()];
}

void ShaderStorage::precompile_shaders(std::vector<std::filesystem::path> paths) {
    std::vector<std::jthread> ths;
    ths.resize(std::max(std::thread::hardware_concurrency(), 4u));
    std::vector<VkShaderModule> mods(paths.size());
    std::vector<VkShaderStageFlagBits> stages(paths.size());
    uint32_t per_th = std::ceilf((float)paths.size() / ths.size());
    for(uint32_t i = 0; i < paths.size(); i += per_th) {
        canonize_path(paths.at(i));
        uint32_t count = std::min(per_th, (uint32_t)paths.size() - i);
        if(count == 0) { break; }
        ths.at(i / per_th) = std::jthread{ [this, i, count, &mods, &stages, &paths] {
            for(uint32_t j = i; j < i + count; ++j) {
                stages[j] = get_stage(paths[j]);
                mods[j] = compile_shader(paths[j]);
            }
        } };
    }
    for(auto& e : ths) {
        if(e.joinable()) { e.join(); }
    }
    for(uint32_t i = 0; i < paths.size(); ++i) {
        if(mods.at(i)) { metadatas[paths.at(i)] = ShaderMetadata{ .shader = mods.at(i), .stage = stages.at(i) }; }
    }
}

VkShaderModule ShaderStorage::get_shader(std::filesystem::path path) {
    canonize_path(path);
    if(metadatas.contains(path)) { return metadatas.at(path).shader; }
    auto t = get_stage(path);
    auto s = compile_shader(path);
    if(s) { metadatas[path] = ShaderMetadata{ .shader = s, .stage = t }; }
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
    if(path.extension() != ".inc") { ENG_WARN("Unrecognized shader extension {}", path.string()); }
    return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
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
        return static_cast<decltype(shaderc_vertex_shader)>(~(std::underlying_type_t<decltype(shaderc_vertex_shader)>{}));
    }();

    if(~std::underlying_type_t<decltype(shaderc_vertex_shader)>(kind) == 0) { return {}; }

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

    auto module_info = Vks(VkShaderModuleCreateInfo{
        .codeSize = (res.end() - res.begin()) * sizeof(uint32_t),
        .pCode = res.begin(),
    });
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(get_renderer().dev, &module_info, nullptr, &mod));
    return mod;
}

void ShaderStorage::canonize_path(std::filesystem::path& p) {
    static const auto prefix = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders");
    if(!p.string().starts_with(prefix.string())) { p = prefix / p; }
    p.make_preferred();
}

Fence::Fence(VkDevice dev, bool signaled) {
    auto info = Vks(VkFenceCreateInfo{});
    if(signaled) { info.flags |= VK_FENCE_CREATE_SIGNALED_BIT; }
    VK_CHECK(vkCreateFence(dev, &info, nullptr, &fence));
}

Fence::Fence(Fence&& f) noexcept { *this = std::move(f); }

Fence& Fence::operator=(Fence&& f) noexcept {
    fence = std::exchange(f.fence, nullptr);
    return *this;
}

Fence::~Fence() noexcept { vkDestroyFence(get_renderer().dev, fence, nullptr); }

VkResult Fence::wait(uint64_t timeout) { return vkWaitForFences(get_renderer().dev, 1, &fence, true, timeout); }

void Swapchain::create(uint32_t image_count, uint32_t width, uint32_t height) {
    auto sinfo = Vks(VkSwapchainCreateInfoKHR{
        // sinfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        // sinfo.pNext = &format_list_info;
        .surface = get_renderer().window_surface,
        .minImageCount = image_count,
        .imageFormat = VK_FORMAT_R8G8B8A8_SRGB,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = VkExtent2D{ width, height },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = true,
    });

    if(swapchain) { vkDestroySwapchainKHR(get_renderer().dev, swapchain, nullptr); }
    VK_CHECK(vkCreateSwapchainKHR(get_renderer().dev, &sinfo, nullptr, &swapchain));
    std::vector<VkImage> vk_images(image_count);
    images.resize(image_count);

    VK_CHECK(vkGetSwapchainImagesKHR(get_renderer().dev, swapchain, &image_count, vk_images.data()));

    for(uint32_t i = 0; i < image_count; ++i) {
        images[i] = Image{
            .image = vk_images[i],
            .format = sinfo.imageFormat,
            .usage = sinfo.imageUsage,
            .extent = VkExtent3D{ .width = sinfo.imageExtent.width, .height = sinfo.imageExtent.height, .depth = 1u },
            .mips = 1,
            .layers = 1,
        };
        images[i]._deduce_aspect(sinfo.imageUsage);
        images[i]._create_default_view(2);
        set_debug_name(images[i].image, std::format("swapchain_image_{}", i));
        set_debug_name(images[i].view, std::format("swapchain_image_default_view_{}", i));
    }
}

uint32_t Swapchain::acquire(VkResult* res, uint64_t timeout, VkSemaphore semaphore, VkFence fence) {
    uint32_t idx;
    auto result = vkAcquireNextImageKHR(get_renderer().dev, swapchain, timeout, semaphore, fence, &idx);
    if(res) { *res = result; }
    return idx;
}

Semaphore::Semaphore(VkDevice dev, bool timeline) {
    auto info = Vks(VkSemaphoreCreateInfo{});
    auto tinfo = Vks(VkSemaphoreTypeCreateInfo{
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    });
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

void Queue::submit(const QueueSubmission& submissions, Fence* fence) { submit(std::span{ &submissions, 1 }, fence); }

void Queue::submit(std::span<const QueueSubmission> submissions, Fence* fence) {
    std::vector<VkSubmitInfo2> subs(submissions.size());
    for(uint32_t i = 0; i < submissions.size(); ++i) {
        const auto& sub = submissions[i];
        subs[i] = Vks(VkSubmitInfo2{
            .waitSemaphoreInfoCount = (uint32_t)sub.wait_sems.size(),
            .pWaitSemaphoreInfos = sub.wait_sems.data(),
            .commandBufferInfoCount = (uint32_t)sub.cmds.size(),
            .pCommandBufferInfos = sub.cmds.data(),
            .signalSemaphoreInfoCount = (uint32_t)sub.signal_sems.size(),
            .pSignalSemaphoreInfos = sub.signal_sems.data(),
        });
    }
    VK_CHECK(vkQueueSubmit2(queue, subs.size(), subs.data(), fence ? fence->fence : nullptr));
}

void Queue::submit(VkCommandBuffer cmd, Fence* fence) { submit(QueueSubmission{ .cmds = { cmd } }, fence); }

void Queue::submit_wait(VkCommandBuffer cmd) {
    Fence f{ get_renderer().dev, false };
    submit(cmd, &f);
    f.wait();
}

void Queue::wait_idle() { vkQueueWaitIdle(queue); }

rendergraph::RenderGraph& rendergraph::RenderGraph::add_pass(RenderPass pass) {
    const auto type =
        pass.shaders.empty() ? VK_PIPELINE_STAGE_NONE : get_renderer().shader_storage.get_stage(pass.shaders.front());
    if(type & (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT)) {
        pass.pipeline_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if(pass.pipeline_settings.valueless_by_exception()) { pass.pipeline_settings = RasterizationSettings{}; }
    } else if(type & (VK_SHADER_STAGE_COMPUTE_BIT)) {
        pass.pipeline_bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    } else if(type & (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR)) {
        pass.pipeline_bind_point = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        if(pass.pipeline_settings.valueless_by_exception()) {
            ENG_WARN("Raytracing pipeline pass does not have defined settings. Not adding...");
            return *this;
        }
    }
    passes.push_back(pass);
    return *this;
}

void rendergraph::RenderGraph::bake() {
    struct Barrier {
        VkPipelineStageFlags2 src_stage{}, dst_stage{};
        VkAccessFlags2 src_access{}, dst_access{};
        VkImageLayout src_layout{}, dst_layout{};
    };
    struct LastAccess {
        uint32_t first_read{ UINT32_MAX };
        uint32_t first_write{ UINT32_MAX };
        int32_t last_read{ -1 };
        int32_t last_write{ -1 };
        Barrier last_barrier{};
    };
    std::map<Access::Resource, LastAccess> accesses;

    stages.clear();
    stages.reserve(passes.size());

    // TODO: Maybe multithread this later (shaders for now are all precompiled)
    for(auto& p : passes) {
        if(!p.pipeline) { create_pipeline(p); }
    }

    for(uint32_t i = 0; i < passes.size(); ++i) {
        auto& p = passes.at(i);
        uint32_t stage = 0;
        for(auto& a : p.accesses) {
            if(auto ait = accesses.find(a.resource); ait != accesses.end()) {
                uint32_t last_stage_plus_1 = 0;
                // if READ_WRITE_BIT is set, WRITE_BIT case should handle it
                if((a.type & AccessType::WRITE_BIT) || (a.type == AccessType::NONE_BIT)) {
                    last_stage_plus_1 = std::max(ait->second.last_write, ait->second.last_read) + 1;
                } else if(a.type & AccessType::READ_BIT) {
                    last_stage_plus_1 = ait->second.last_write + 1;
                    if(ait->second.last_read < stages.size() && a.layout != ait->second.last_barrier.dst_layout) {
                        last_stage_plus_1 = std::max(ait->second.last_write, ait->second.last_read) + 1;
                    }
                } else {
                    ENG_WARN("Unrecognized Access type. Skipping.");
                    continue;
                }
                stage = std::max(stage, last_stage_plus_1);
            }
        }
        if(stages.size() <= stage) { stages.resize(stage + 1); }
        auto& s = stages.at(stage);
        s.passes.push_back(i);

        for(auto& a : p.accesses) {
            auto& la = accesses[a.resource];
            la.last_barrier.src_stage = la.last_barrier.dst_stage;
            la.last_barrier.src_access = la.last_barrier.dst_access;
            la.last_barrier.src_layout = la.last_barrier.dst_layout;
            la.last_barrier.dst_stage = a.stage;
            la.last_barrier.dst_access = a.access;
            la.last_barrier.dst_layout = a.layout;
            if(a.type & AccessType::READ_BIT) {
                la.last_read = stage;
                la.first_read = std::min(la.first_read, stage);
            }
            if(a.type & AccessType::WRITE_BIT) {
                la.last_write = stage;
                la.first_write = std::min(la.first_write, stage);
            }

            if(a.resource.type & ResourceType::ANY_IMAGE) {
                // swapchain requires special handling, and it's barrier will be the only one
                // amongst stage image_barriers with barrier's image set to nullptr.
                if(a.resource.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT) {
                    assert(!s.swapchain_barrier);
                    s.swapchain_barrier = [la](uint32_t index) {
                        return get_renderer().swapchain.images.at(index).image;
                    };
                }
                const auto* img = a.resource.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT
                                      ? nullptr
                                      : &get_renderer().images.at(a.resource.resource_idx);
                s.image_barriers.push_back(Vks(VkImageMemoryBarrier2{ .srcStageMask = la.last_barrier.src_stage,
                                                                      .srcAccessMask = la.last_barrier.src_access,
                                                                      .dstStageMask = la.last_barrier.dst_stage,
                                                                      .dstAccessMask = la.last_barrier.dst_access,
                                                                      .oldLayout = la.last_barrier.src_layout,
                                                                      .newLayout = la.last_barrier.dst_layout,
                                                                      .image = !img ? nullptr : img->image,
                                                                      .subresourceRange = { .aspectMask = img ? img->aspect : VK_IMAGE_ASPECT_COLOR_BIT,
                                                                                            .baseMipLevel = 0,
                                                                                            .levelCount = 1,
                                                                                            .baseArrayLayer = 0,
                                                                                            .layerCount = 1 } }));
            } else if(a.resource.type == ResourceType::STORAGE_BUFFER) {
                s.buffer_barriers.push_back(Vks(VkBufferMemoryBarrier2{
                    .srcStageMask = la.last_barrier.src_stage,
                    .srcAccessMask = la.last_barrier.src_access,
                    .dstStageMask = la.last_barrier.dst_stage,
                    .dstAccessMask = la.last_barrier.dst_access,
                    .buffer = get_renderer().buffers.at(a.resource.resource_idx).buffer,
                    .offset = 0,
                    .size = VK_WHOLE_SIZE,
                }));
            } else if(a.resource.type == ResourceType::ACCELERATION_STRUCTURE) {
                assert(false && "Don't know as of yet if this requires additional handling here.");
            } else {
                assert(false);
            }
        }
    }

    // modify the old layout of the first barrier for the image resource type
    // so that it is same as the last barrier's new layout.
    std::vector<VkImageMemoryBarrier2> initial_barriers;
    for(const auto& a : accesses) {
        if(!(a.first.type & ResourceType::ANY_IMAGE)) { continue; }
        const auto first_stage = std::min(a.second.first_read, a.second.first_write);
        const auto last_stage = std::max(a.second.last_read, a.second.last_write);
        assert(first_stage < stages.size());
        assert(last_stage > -1 && last_stage < stages.size());
        auto& first_barrier =
            *std::find_if(stages.at(first_stage).image_barriers.begin(), stages.at(first_stage).image_barriers.end(),
                          [img = (a.first.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT)
                                     ? nullptr
                                     : get_renderer().images.at(a.first.resource_idx).image](const auto& b) {
                              return b.image == img;
                          });
        const auto& last_barrier = a.second.last_barrier;
        if(a.first.flags & ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT) { continue; }
        first_barrier.oldLayout = last_barrier.dst_layout;
        initial_barriers.push_back(Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                                                              .srcAccessMask = VK_ACCESS_2_NONE,
                                                              .dstStageMask = first_barrier.dstStageMask,
                                                              .dstAccessMask = first_barrier.dstAccessMask,
                                                              .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                              .newLayout = first_barrier.oldLayout,
                                                              .image = first_barrier.image,
                                                              .subresourceRange = first_barrier.subresourceRange }));
        if(stages.at(first_stage).swapchain_barrier && !first_barrier.image) {
            // if it's swapchain image, they'll all need the same starting layout, so just make them be so here.
            initial_barriers.back().image = stages.at(first_stage).swapchain_barrier(0);
            for(uint32_t i = 1; i < get_renderer().swapchain.images.size(); ++i) {
                initial_barriers.push_back(initial_barriers.back());
                initial_barriers.back().image = stages.at(first_stage).swapchain_barrier(i);
            }
        }
    }
    auto initial_dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = static_cast<uint32_t>(initial_barriers.size()),
                                                  .pImageMemoryBarriers = initial_barriers.data() });
    auto cmd = get_renderer().get_frame_data().cmdpool->begin_onetime();
    vkCmdPipelineBarrier2(cmd, &initial_dep_info);
    get_renderer().get_frame_data().cmdpool->end(cmd);
    Fence f{ get_renderer().dev, false };
    get_renderer().gq.submit(cmd, &f);

    // make all the swapchain barriers with image == nullptr go to the front for
    // easy replacing.
    for(auto& p : stages) {
        if(!p.swapchain_barrier) { continue; }
        for(uint32_t i = 0; i < p.image_barriers.size(); ++i) {
            if(!p.image_barriers.at(i).image) {
                std::swap(p.image_barriers.at(0), p.image_barriers.at(i));
                break;
            }
        }
    }

    f.wait();
}

void rendergraph::RenderGraph::create_pipeline(RenderPass& pass) {
    if(pass.shaders.empty()) { return; }

    for(const auto& p : passes) {
        if(!p.pipeline) { break; }
        if(pass.shaders.size() != p.shaders.size() || pass.pipeline_bind_point != p.pipeline_bind_point ||
           !std::visit(Visitor{
                           [&p](const RasterizationSettings& a) {
                               return a == *std::get_if<RasterizationSettings>(&p.pipeline_settings);
                           },
                           [&p](const RaytracingSettings& a) {
                               return a == *std::get_if<RaytracingSettings>(&p.pipeline_settings);
                           },
                       },
                       pass.pipeline_settings) ||
           ![&pass, &p] {
               for(const auto& s : pass.shaders) {
                   if(std::find(p.shaders.begin(), p.shaders.end(), s) == p.shaders.end()) { return false; }
               }
               return true;
           }()) {
            continue;
        }
        pass.pipeline = p.pipeline;
        return;
    }

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for(const auto& p : pass.shaders) {
        auto stage = Vks(VkPipelineShaderStageCreateInfo{
            .stage = get_renderer().shader_storage.get_stage(p),
            .module = get_renderer().shader_storage.get_shader(p),
            .pName = "main",
        });
        stages.push_back(stage);
    }

    VkPipeline pipeline{};

    if(pass.pipeline_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        auto& rasterization_settings = *std::get_if<RasterizationSettings>(&pass.pipeline_settings);

        auto pVertexInputState = Vks(VkPipelineVertexInputStateCreateInfo{});

        auto pInputAssemblyState = Vks(VkPipelineInputAssemblyStateCreateInfo{
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = false,
        });

        auto pTessellationState = Vks(VkPipelineTessellationStateCreateInfo{});

        auto pViewportState = Vks(VkPipelineViewportStateCreateInfo{});

        auto pRasterizationState = Vks(VkPipelineRasterizationStateCreateInfo{
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = rasterization_settings.culling,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        });

        auto pMultisampleState = Vks(VkPipelineMultisampleStateCreateInfo{
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        });

        auto pDepthStencilState = Vks(VkPipelineDepthStencilStateCreateInfo{
            .depthTestEnable = rasterization_settings.depth_test,
            .depthWriteEnable = rasterization_settings.depth_write,
            .depthCompareOp = rasterization_settings.depth_op,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
            .front = {},
            .back = {},
        });

        std::array<VkPipelineColorBlendAttachmentState, 4> blends;
        for(uint32_t i = 0; i < rasterization_settings.num_col_formats; ++i) {
            blends[i] = { .colorWriteMask = 0b1111 /*RGBA*/ };
        }
        auto pColorBlendState = Vks(VkPipelineColorBlendStateCreateInfo{
            .attachmentCount = rasterization_settings.num_col_formats,
            .pAttachments = blends.data(),
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
            .colorAttachmentCount = rasterization_settings.num_col_formats,
            .pColorAttachmentFormats = rasterization_settings.col_formats.data(),
            .depthAttachmentFormat = rasterization_settings.dep_format,
        });
        // pDynamicRendering.stencilAttachmentFormat = rasterization_settings.st_format;

        auto info = Vks(VkGraphicsPipelineCreateInfo{
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
            .layout = get_renderer().bindless_layout.layout,
        });
        VK_CHECK(vkCreateGraphicsPipelines(get_renderer().dev, nullptr, 1, &info, nullptr, &pipeline));
    } else if(pass.pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
        assert(stages.size() == 1);
        auto info = Vks(VkComputePipelineCreateInfo{
            .stage = stages.at(0),
            .layout = get_renderer().bindless_layout.layout,
        });
        VK_CHECK(vkCreateComputePipelines(get_renderer().dev, nullptr, 1, &info, nullptr, &pipeline));
    } else if(pass.pipeline_bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
        auto& settings = *std::get_if<RaytracingSettings>(&pass.pipeline_settings);
        auto info = Vks(VkRayTracingPipelineCreateInfoKHR{
            .stageCount = (uint32_t)stages.size(),
            .pStages = stages.data(),
            .groupCount = (uint32_t)settings.groups.size(),
            .pGroups = settings.groups.data(),
            .maxPipelineRayRecursionDepth = settings.recursion_depth,
            .layout = get_renderer().bindless_layout.layout,
        });
        VK_CHECK(vkCreateRayTracingPipelinesKHR(get_renderer().dev, nullptr, nullptr, 1, &info, nullptr, &pipeline));

        const uint32_t handleSize = get_renderer().rt_props.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = align_up(handleSize, get_renderer().rt_props.shaderGroupHandleAlignment);
        const uint32_t groupCount = static_cast<uint32_t>(settings.groups.size());
        const uint32_t sbtSize = groupCount * handleSizeAligned;

        std::vector<uint8_t> shaderHandleStorage(sbtSize);
        vkGetRayTracingShaderGroupHandlesKHR(get_renderer().dev, pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

        const VkBufferUsageFlags bufferUsageFlags =
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        settings.sbt = get_renderer().make_buffer("buffer_sbt", sbtSize, bufferUsageFlags);
        get_renderer().send_to(settings.sbt, 0ull, &shaderHandleStorage, sizeof(shaderHandleStorage));
    } else {
        assert(false);
    }
    pipelines.push_back(Pipeline{ .pipeline = pipeline });
    pass.pipeline = &pipelines.back();
}

void rendergraph::RenderGraph::render(VkCommandBuffer cmd, uint32_t swapchain_index) {
    for(auto& s : stages) {
        if(s.swapchain_barrier) { s.image_barriers.at(0).image = s.swapchain_barrier(swapchain_index); }
        auto dep_info = Vks(VkDependencyInfo{
            .bufferMemoryBarrierCount = static_cast<uint32_t>(s.buffer_barriers.size()),
            .pBufferMemoryBarriers = s.buffer_barriers.data(),
            .imageMemoryBarrierCount = static_cast<uint32_t>(s.image_barriers.size()),
            .pImageMemoryBarriers = s.image_barriers.data(),
        });
        vkCmdPipelineBarrier2(cmd, &dep_info);
        for(auto p : s.passes) {
            auto& pass = passes.at(p);
            if(pass.pipeline) { vkCmdBindPipeline(cmd, pass.pipeline_bind_point, pass.pipeline->pipeline); }
            if(pass.callback_render) { pass.callback_render(cmd, swapchain_index, pass); }
        }
    }
}

StagingBuffer::StagingBuffer() {
    buffer = get_renderer().allocate_buffer("staging_ring_buffer", 1024 * 1024 * 32,
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, 8u);
}

bool StagingBuffer::send(Buffer& dst, size_t dst_offset, std::span<const std::byte> src) {
    if(buffer.capacity - buffer.size < src.size_bytes()) {
        // TODO:
        ENG_WARN("Resource too big. split into parts");
        return false;
    }
    memcpy((std::byte*)buffer.memory + buffer.size, src.data(), src.size_bytes());
    VkBufferCopy copy{ .srcOffset = buffer.size, .dstOffset = dst_offset, .size = src.size_bytes() };
    buffer.size += src.size_bytes();
    if(!cmd) {
        begin();
        vkCmdCopyBuffer(cmd, buffer.buffer, dst.buffer, 1, &copy);
        stage();
    } else {
        vkCmdCopyBuffer(cmd, buffer.buffer, dst.buffer, 1, &copy);
    }
    return true;
}

bool StagingBuffer::send(Buffer& dst, size_t dst_offset, Buffer& src, size_t src_offset, size_t size) {
    VkBufferCopy copy{ .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
    if(!cmd) {
        begin();
        vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &copy);
        stage();
    } else {
        vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &copy);
    }
    return true;
}

bool StagingBuffer::send(Image& dst, std::span<const std::byte> src, VkBufferImageCopy copy) {
    if(buffer.capacity - buffer.size < src.size_bytes()) {
        // TODO:
        ENG_WARN("Resource too big. split into parts");
        return false;
    }
    const auto old_layout = dst.current_layout;
    dst.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    memcpy((std::byte*)buffer.memory + buffer.size, src.data(), src.size_bytes());
    copy.bufferOffset = buffer.size;
    buffer.size += src.size_bytes();
    if(!cmd) {
        begin();
        dst.transition_layout(cmd, VK_PIPELINE_STAGE_2_NONE_KHR, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, buffer.buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        dst.transition_layout(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, old_layout);
        stage();
    } else {
        dst.transition_layout(cmd, VK_PIPELINE_STAGE_2_NONE_KHR, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage(cmd, buffer.buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        dst.transition_layout(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, old_layout);
    }
    return true;
}

void StagingBuffer::begin() {
    if(cmd) {
        ENG_WARN("Starting new batch when previous one was staged yet.");
        (*pool)->end(cmd);
    }
    pool = &get_renderer().get_frame_data().cmdpool;
    cmd = (*pool)->begin_onetime();
}

void StagingBuffer::stage() {
    (*pool)->end(cmd);
    get_renderer().gq.submit_wait(cmd);
    buffer.size = 0;
    cmd = nullptr;
}
