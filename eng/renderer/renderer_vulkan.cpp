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
#include <eng/engine.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/utils.hpp>
#include <assets/shaders/bindless_structures.inc>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/descpool.hpp>
#include <eng/renderer/submit_queue.hpp>

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

void RendererVulkan::initialize_imgui() {
    IMGUI_CHECKVERSION();
    void* user_data;
    Engine::get().ui_ctx->imgui_ctx = ImGui::CreateContext();
    ImGui::GetAllocatorFunctions(&Engine::get().ui_ctx->alloc_callbacks->imgui_alloc,
                                 &Engine::get().ui_ctx->alloc_callbacks->imgui_free, &user_data);
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
        .QueueFamily = submit_queue->family_idx,
        .Queue = submit_queue->queue,
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

    auto cmdimgui = get_frame_data().cmdpool->begin();
    ImGui_ImplVulkan_CreateFontsTexture();
    get_frame_data().cmdpool->end(cmdimgui);
    submit_queue->with_cmd_buf(cmdimgui).submit_wait(-1ULL);
}

void RendererVulkan::initialize_resources() {
    bindless_pool = new BindlessDescriptorPool{ dev };

    staging_buffer = new StagingBuffer{
        submit_queue,
        make_buffer("staging_buffer", buffer_resizable,
                    Vks(VkBufferCreateInfo{ .size = 64 * 1024 * 1024, .usage = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR }),
                    VmaAllocationCreateInfo{ .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                                             .usage = VMA_MEMORY_USAGE_AUTO,
                                             .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT })
    };

    vertex_positions_buffer =
        make_buffer("vertex_positions_buffer", buffer_resizable,
                    Vks(VkBufferCreateInfo{
                        .size = 1024ull,
                        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    }),
                    VmaAllocationCreateInfo{});
    vertex_attributes_buffer =
        make_buffer("vertex_attributes_buffer", buffer_resizable,
                    Vks(VkBufferCreateInfo{ .size = 1024ull, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT }),
                    VmaAllocationCreateInfo{});
    index_buffer =
        make_buffer("index_buffer", buffer_resizable,
                    Vks(VkBufferCreateInfo{ .size = 1024ull,
                                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT }),
                    VmaAllocationCreateInfo{});

    // auto samp_ne = samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // auto samp_ll = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    // auto samp_lr = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

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
        fd.render_graph = rendergraph::RenderGraph{ dev };
        fd.cmdpool = submit_queue->make_command_pool();
        fd.sem_swapchain = submit_queue->make_semaphore();
        fd.sem_rendering_finished = submit_queue->make_semaphore();
        fd.fen_rendering_finished = submit_queue->make_fence(true);
        fd.constants =
            make_buffer(std::format("constants_{}", i),
                        Vks(VkBufferCreateInfo{ .size = 512ul, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT }),
                        VmaAllocationCreateInfo{});
        fd.transform_buffers =
            make_buffer(std::format("transform_buffer_{}", i),
                        Vks(VkBufferCreateInfo{ .size = 512ul, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT }),
                        VmaAllocationCreateInfo{});
    }

    vsm.constants_buffer = make_buffer("vms buffer",
                                       Vks(VkBufferCreateInfo{ .size = sizeof(GPUVsmConstantsBuffer) + 64 * 64 * 4,
                                                               .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT }),
                                       VmaAllocationCreateInfo{});
    GPUVsmConstantsBuffer vsm_constants{
        .dir_light_view = glm::mat4{ 1.0f },
        .num_pages_xy = 64,
        .max_clipmap_index = 0,
        .texel_resolution = 1024.0f * 8.0f,
    };
    // send_to(vsm.constants_buffer, 0, &vsm_constants, sizeof(vsm_constants));
    vsm.free_allocs_buffer =
        make_buffer("vms alloc buffer",
                    Vks(VkBufferCreateInfo{ .size = sizeof(GPUVsmAllocConstantsBuffer), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT }),
                    VmaAllocationCreateInfo{});
    GPUVsmAllocConstantsBuffer vsm_allocs{ .free_list_head = 0, .free_list = {} };
    staging_buffer->send_to(vsm.constants_buffer, 0, vsm_constants).send_to(vsm.free_allocs_buffer, 0, &vsm_allocs).submit();
    // send_to(vsm.free_allocs_buffer, 0, &vsm_allocs, sizeof(vsm_allocs));

    vsm.shadow_map_0 = make_image("vsm image", VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                                                  .format = VK_FORMAT_R32_SFLOAT,
                                                                  .extent = { 1024 * 8, 1024 * 8, 1 },
                                                                  .mipLevels = 1,
                                                                  .arrayLayers = 1,
                                                                  .samples = VK_SAMPLE_COUNT_1_BIT,
                                                                  .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                                           VK_IMAGE_USAGE_TRANSFER_DST_BIT });
    vsm.view_shadow_map_0_general = make_image_view(vsm.shadow_map_0, VK_IMAGE_LAYOUT_GENERAL, nullptr);

    vsm.dir_light_page_table =
        make_image("vsm dir light 0 page table",
                   VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                      .format = VK_FORMAT_R32_SFLOAT,
                                      .extent = { vsm_constants.num_pages_xy, vsm_constants.num_pages_xy, 1 },
                                      .mipLevels = 1,
                                      .arrayLayers = 1,
                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                      .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT });
    vsm.view_dir_light_page_table_general = make_image_view(vsm.dir_light_page_table, VK_IMAGE_LAYOUT_GENERAL, nullptr);

    vsm.dir_light_page_table_rgb8 =
        make_image("vsm dir light 0 page table rgb8",
                   VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                      .format = VK_FORMAT_R8G8B8A8_UNORM,
                                      .extent = { vsm_constants.num_pages_xy, vsm_constants.num_pages_xy, 1 },
                                      .mipLevels = 1,
                                      .arrayLayers = 1,
                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });
    vsm.view_dir_light_page_table_rgb8_general = make_image_view(vsm.dir_light_page_table_rgb8, VK_IMAGE_LAYOUT_GENERAL, nullptr);

    create_window_sized_resources();

    {
        std::vector<std::filesystem::path> shaders;
        for(auto it : std::filesystem::recursive_directory_iterator{ std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders" }) {
            if(!it.is_regular_file()) { continue; }
            shaders.push_back(it.path());
        }
        shader_storage.precompile_shaders(shaders);
    }

    build_render_graph();
}

void RendererVulkan::create_window_sized_resources() {
    swapchain.create(dev, frame_datas.size(), Engine::get().window->width, Engine::get().window->height);
    for(auto i = 0; i < frame_datas.size(); ++i) {
        auto& fd = frame_datas.at(i);
        fd.gbuffer.color_image =
            make_image(std::format("g_color_{}", i),
                       VkImageCreateInfo{
                           .imageType = VK_IMAGE_TYPE_2D,
                           .format = VK_FORMAT_R8G8B8A8_SRGB,
                           .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height, 1 },
                           .mipLevels = 1,
                           .arrayLayers = 1,
                           .samples = VK_SAMPLE_COUNT_1_BIT,
                           .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        fd.gbuffer.depth_buffer_image =
            make_image(std::format("g_depth_{}", i),
                       VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                          .format = VK_FORMAT_D24_UNORM_S8_UINT,
                                          .extent = { (uint32_t)Engine::get().window->width,
                                                      (uint32_t)Engine::get().window->height, 1 },
                                          .mipLevels = 1,
                                          .arrayLayers = 1,
                                          .samples = VK_SAMPLE_COUNT_1_BIT,
                                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        fd.gbuffer.view_depth_buffer_image_ronly_lr =
            make_image_view(fd.gbuffer.depth_buffer_image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                            samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT));

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
        fd.render_graph.render_list.clear();
        auto* vsm_clear_pages = fd.render_graph.make_pass();
        auto* z_prepass = fd.render_graph.make_pass();
        auto* vsm_page_alloc = fd.render_graph.make_pass();
        auto* vsm_shadow = fd.render_graph.make_pass();
        auto* default_unlit = fd.render_graph.make_pass();
        auto* vsm_debug_page_copy = fd.render_graph.make_pass();
        auto* imgui = fd.render_graph.make_pass();
        auto* swapchain_present = fd.render_graph.make_pass();

        *vsm_clear_pages = rendergraph::RenderPass{
            .name = "vsm/clear_pages",
            .accesses = { rendergraph::Access{ .resource = vsm.dir_light_page_table,
                                               .flags = rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                                               .type = rendergraph::AccessType::WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL },
                          rendergraph::Access{ .resource = vsm.shadow_map_0,
                                               .flags = rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                                               .type = rendergraph::AccessType::WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                               .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL } },
            .shaders = { "vsm/clear_page.comp.glsl" },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                    auto& r = *RendererVulkan::get_instance();
                    uint32_t bindless_indices[]{
                        r.get_bindless_index(r.index_buffer),
                        r.get_bindless_index(r.vertex_positions_buffer),
                        r.get_bindless_index(r.vertex_attributes_buffer),
                        r.get_bindless_index(r.get_frame_data().transform_buffers),
                        r.get_bindless_index(r.vsm.constants_buffer),
                        r.get_bindless_index(r.vsm.free_allocs_buffer),
                        r.get_bindless_index(r.get_frame_data().gbuffer.view_depth_buffer_image_ronly_lr),
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
                        r.get_bindless_index(r.get_frame_data().constants),
                        0,
                    };
                    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                       sizeof(bindless_indices), bindless_indices);
                    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
                    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
                    VkClearColorValue clear_value{ .float32 = { 1.0f, 0.0f, 0.0f, 0.0f } };
                    VkImageSubresourceRange clear_range{
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
                    };
                    vkCmdClearColorImage(cmd, r.get_image(r.vsm.shadow_map_0).image,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1, &clear_range);
                }
        };

        *z_prepass = rendergraph::RenderPass{
            .name = "z_prepass",
            .accesses = { rendergraph::Access{ .resource = fd.gbuffer.depth_buffer_image,
                                               .flags = rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                                               .type = rendergraph::AccessType::READ_WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                               .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL } },
            .shaders = { "vsm/zprepass.vert.glsl", "vsm/zprepass.frag.glsl" },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                    auto& r = *RendererVulkan::get_instance();
                    const auto r_dep_att = Vks(VkRenderingAttachmentInfo{
                        .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).get_view(),
                        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue = { .depthStencil = { 1.0f, 0 } },
                    });
                    const auto rendering_info = Vks(VkRenderingInfo{
                        .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
                                                    .height = (uint32_t)Engine::get().window->height } },
                        .layerCount = 1,
                        .colorAttachmentCount = 0,
                        .pColorAttachments = nullptr,
                        .pDepthAttachment = &r_dep_att,
                    });
                    vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdBeginRendering(cmd, &rendering_info);
                    VkRect2D r_sciss_1 = rendering_info.renderArea;
                    VkViewport r_view_1{ .x = 0.0f,
                                         .y = 0.0f,
                                         .width = (float)rendering_info.renderArea.extent.width,
                                         .height = (float)rendering_info.renderArea.extent.height,
                                         .minDepth = 0.0f,
                                         .maxDepth = 1.0f };
                    vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
                    vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
                    uint32_t bindless_indices[]{
                        r.get_bindless_index(r.index_buffer),
                        r.get_bindless_index(r.vertex_positions_buffer),
                        r.get_bindless_index(r.vertex_attributes_buffer),
                        r.get_bindless_index(r.get_frame_data().transform_buffers),
                        r.get_bindless_index(r.vsm.constants_buffer),
                        r.get_bindless_index(r.vsm.free_allocs_buffer),
                        r.get_bindless_index(r.get_frame_data().gbuffer.view_depth_buffer_image_ronly_lr),
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
                        r.get_bindless_index(r.get_frame_data().constants),
                        0,
                    };
                    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                       sizeof(bindless_indices), bindless_indices);
                    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
                    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                                  sizeof(IndirectDrawCommandBufferHeader),
                                                  r.get_buffer(r.indirect_draw_buffer).buffer, 0ull, r.max_draw_count,
                                                  sizeof(VkDrawIndexedIndirectCommand));
                    vkCmdEndRendering(cmd);
                }
        };

        *vsm_page_alloc = rendergraph::RenderPass{
            .name = "vsm/page_alloc",
            .accesses = { rendergraph::Access{ .resource = fd.gbuffer.depth_buffer_image,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                          rendergraph::Access{ .resource = vsm.dir_light_page_table,
                                               .type = rendergraph::AccessType::READ_WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL },
                          rendergraph::Access{ .resource = vsm.free_allocs_buffer,
                                               .type = rendergraph::AccessType::READ_WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT } },
            .shaders = { "vsm/page_alloc.comp.glsl" },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                    auto& r = *RendererVulkan::get_instance();
                    uint32_t bindless_indices[]{
                        r.get_bindless_index(r.index_buffer),
                        r.get_bindless_index(r.vertex_positions_buffer),
                        r.get_bindless_index(r.vertex_attributes_buffer),
                        r.get_bindless_index(r.get_frame_data().transform_buffers),
                        r.get_bindless_index(r.vsm.constants_buffer),
                        r.get_bindless_index(r.vsm.free_allocs_buffer),
                        r.get_bindless_index(r.get_frame_data().gbuffer.view_depth_buffer_image_ronly_lr),
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
                        r.get_bindless_index(r.get_frame_data().constants),
                        0,
                    };
                    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                       sizeof(bindless_indices), bindless_indices);
                    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
                    vkCmdDispatch(cmd, (uint32_t)std::ceilf(Engine::get().window->width / 8.0f),
                                  (uint32_t)std::ceilf(Engine::get().window->height / 8.0f), 1);
                }
        };

        *vsm_shadow = rendergraph::RenderPass{
            .name = "vsm/shadow",
            .accesses = { rendergraph::Access{ .resource = vsm.dir_light_page_table,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL },
                          rendergraph::Access{ .resource = vsm.shadow_map_0,
                                               .type = rendergraph::AccessType::READ_WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL } },
            .shaders = { "vsm/shadow.vert.glsl", "vsm/shadow.frag.glsl" },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                    auto& r = *RendererVulkan::get_instance();
                    const auto rendering_info = Vks(VkRenderingInfo{
                        .renderArea = { .extent = { .width = r.get_image(r.vsm.shadow_map_0).vk_info.extent.width,
                                                    .height = r.get_image(r.vsm.shadow_map_0).vk_info.extent.height } },
                        .layerCount = 1,
                        .colorAttachmentCount = 0,
                        .pColorAttachments = nullptr,
                        .pDepthAttachment = nullptr,
                    });
                    vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdBeginRendering(cmd, &rendering_info);
                    VkRect2D r_sciss_1 = rendering_info.renderArea;
                    VkViewport r_view_1{ .x = 0.0f,
                                         .y = 0.0f,
                                         .width = (float)rendering_info.renderArea.extent.width,
                                         .height = (float)rendering_info.renderArea.extent.height,
                                         .minDepth = 0.0f,
                                         .maxDepth = 1.0f };
                    vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
                    vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
                    uint32_t bindless_indices[]{
                        r.get_bindless_index(r.index_buffer),
                        r.get_bindless_index(r.vertex_positions_buffer),
                        r.get_bindless_index(r.vertex_attributes_buffer),
                        r.get_bindless_index(r.get_frame_data().transform_buffers),
                        r.get_bindless_index(r.vsm.constants_buffer),
                        r.get_bindless_index(r.vsm.free_allocs_buffer),
                        r.get_bindless_index(r.get_frame_data().gbuffer.view_depth_buffer_image_ronly_lr),
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
                        r.get_bindless_index(r.get_frame_data().constants),
                        0,
                        r.get_bindless_index(r.vsm.view_shadow_map_0_general),
                    };
                    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                       sizeof(bindless_indices), bindless_indices);
                    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
                    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                                  sizeof(IndirectDrawCommandBufferHeader),
                                                  r.get_buffer(r.indirect_draw_buffer).buffer, 0ull, r.max_draw_count,
                                                  sizeof(VkDrawIndexedIndirectCommand));
                    vkCmdEndRendering(cmd);
                }
        };

        *default_unlit = rendergraph::RenderPass{
            .name = "vsm/default_unlit",
            .accesses = { rendergraph::Access{ .resource = fd.gbuffer.color_image,
                                               .flags = rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                                               .type = rendergraph::AccessType::WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                               .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                          rendergraph::Access{ .resource = fd.gbuffer.depth_buffer_image,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                                               .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                          rendergraph::Access{ .resource = vsm.shadow_map_0,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL },
                          rendergraph::Access{ .resource = vsm.dir_light_page_table,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL } },
            .shaders = { "default_unlit/unlit.vert.glsl", "default_unlit/unlit.frag.glsl" },
            .pipeline_settings = rendergraph::RasterizationSettings{ .depth_test = true, .depth_write = false, .depth_op = VK_COMPARE_OP_LESS_OR_EQUAL },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                    auto& r = *RendererVulkan::get_instance();
                    auto r_col_att_1 = Vks(VkRenderingAttachmentInfo{
                        .imageView = r.get_image(r.get_frame_data().gbuffer.color_image).get_view(),
                        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
                    });

                    VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };
                    auto r_dep_att = Vks(VkRenderingAttachmentInfo{
                        .imageView = r.get_image(r.get_frame_data().gbuffer.depth_buffer_image).get_view(),
                        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                        .storeOp = VK_ATTACHMENT_STORE_OP_NONE,
                        .clearValue = { .depthStencil = { 1.0f, 0 } },
                    });
                    auto rendering_info = Vks(VkRenderingInfo{
                        .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
                                                    .height = (uint32_t)Engine::get().window->height } },
                        .layerCount = 1,
                        .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
                        .pColorAttachments = r_col_atts,
                        .pDepthAttachment = &r_dep_att,
                    });

                    vkCmdBindIndexBuffer(cmd, r.get_buffer(r.index_buffer).buffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdBeginRendering(cmd, &rendering_info);
                    VkRect2D r_sciss_1{
                        .offset = {}, .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height }
                    };
                    VkViewport r_view_1{ .x = 0.0f,
                                         .y = 0.0f,
                                         .width = Engine::get().window->width,
                                         .height = Engine::get().window->height,
                                         .minDepth = 0.0f,
                                         .maxDepth = 1.0f };
                    vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
                    vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
                    uint32_t bindless_indices[]{
                        r.get_bindless_index(r.index_buffer),
                        r.get_bindless_index(r.vertex_positions_buffer),
                        r.get_bindless_index(r.vertex_attributes_buffer),
                        r.get_bindless_index(r.get_frame_data().transform_buffers),
                        r.get_bindless_index(r.get_frame_data().constants),
                        r.get_bindless_index(r.mesh_instances_buffer),
                        r.get_bindless_index(r.vsm.constants_buffer),
                        r.get_bindless_index(r.vsm.view_shadow_map_0_general),
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
                    };
                    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                       sizeof(bindless_indices), bindless_indices);
                    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
                    vkCmdDrawIndexedIndirectCount(cmd, r.get_buffer(r.indirect_draw_buffer).buffer,
                                                  sizeof(IndirectDrawCommandBufferHeader),
                                                  r.get_buffer(r.indirect_draw_buffer).buffer, 0ull, r.max_draw_count,
                                                  sizeof(VkDrawIndexedIndirectCommand));
                    vkCmdEndRendering(cmd);
                }
        };

        *vsm_debug_page_copy = rendergraph::RenderPass{
            .accesses = { rendergraph::Access{ .resource = vsm.dir_light_page_table,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL },
                          rendergraph::Access{ .resource = vsm.dir_light_page_table_rgb8,
                                               .type = rendergraph::AccessType::WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_GENERAL } },
            .shaders = { "vsm/debug_page_alloc_copy.comp.glsl" },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass& pass) {
                    auto& r = *RendererVulkan::get_instance();
                    uint32_t bindless_indices[]{
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_general),
                        r.get_bindless_index(r.vsm.view_dir_light_page_table_rgb8_general),
                        r.get_bindless_index(r.vsm.constants_buffer),
                    };
                    vkCmdPushConstants(cmd, r.bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                       sizeof(bindless_indices), bindless_indices);
                    r.bindless_pool->bind(cmd, pass.pipeline_bind_point);
                    vkCmdDispatch(cmd, 64 / 8, 64 / 8, 1);
                },
        };

        *imgui = rendergraph::RenderPass{
            .accesses = { rendergraph::Access{ .resource = Handle<Image>{ rendergraph::swapchain_index },
                                               .flags = rendergraph::ResourceFlags::SWAPCHAIN_IMAGE_BIT |
                                                        rendergraph::ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT,
                                               .type = rendergraph::AccessType::WRITE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                               .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                               .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL },
                          rendergraph::Access{ .resource = fd.gbuffer.color_image,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                          rendergraph::Access{ .resource = vsm.dir_light_page_table_rgb8,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
                          rendergraph::Access{ .resource = vsm.shadow_map_0,
                                               .type = rendergraph::AccessType::READ_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                               .access = VK_ACCESS_2_SHADER_READ_BIT,
                                               .layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL }

            },
            .callback_render =
                [](VkCommandBuffer cmd, uint32_t swapchain_index, rendergraph::RenderPass&) {
                    auto& r = *RendererVulkan::get_instance();
                    ImGui::SetCurrentContext(Engine::get().ui_ctx->imgui_ctx);
                    ImGui_ImplVulkan_NewFrame();
                    ImGui_ImplGlfw_NewFrame();
                    ImGui::NewFrame();
                    ImGuizmo::BeginFrame();
                    eng_ui_update();
                    ImGui::Render();
                    ImDrawData* im_draw_data = ImGui::GetDrawData();
                    if(im_draw_data) {
                        VkRenderingAttachmentInfo r_col_atts[]{
                            Vks(VkRenderingAttachmentInfo{
                                .imageView = RendererVulkan::get_instance()->swapchain.images.at(swapchain_index).get_view(),
                                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
                            }),
                        };
                        VkRect2D r_sciss_1{ .offset = {},
                                            .extent = { (uint32_t)Engine::get().window->width,
                                                        (uint32_t)Engine::get().window->height } };
                        VkViewport r_view_1{ .x = 0.0f,
                                             .y = Engine::get().window->height,
                                             .width = Engine::get().window->width,
                                             .height = Engine::get().window->height,
                                             .minDepth = 0.0f,
                                             .maxDepth = 1.0f };
                        auto rendering_info = Vks(VkRenderingInfo{
                            .renderArea = { .extent = { .width = (uint32_t)Engine::get().window->width,
                                                        .height = (uint32_t)Engine::get().window->height } },
                            .layerCount = 1,
                            .colorAttachmentCount = sizeof(r_col_atts) / sizeof(r_col_atts[0]),
                            .pColorAttachments = r_col_atts,
                        });
                        vkCmdBeginRendering(cmd, &rendering_info);
                        vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
                        vkCmdSetViewport(cmd, 0, 1, &r_view_1);
                        ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
                        vkCmdEndRendering(cmd);
                    }
                },
        };

        *swapchain_present = rendergraph::RenderPass{
            .accesses = { rendergraph::Access{ .resource = Handle<Image>{ rendergraph::swapchain_index },
                                               .flags = rendergraph::ResourceFlags::SWAPCHAIN_IMAGE_BIT,
                                               .type = rendergraph::AccessType::NONE_BIT,
                                               .stage = VK_PIPELINE_STAGE_2_NONE,
                                               .access = VK_ACCESS_2_NONE,
                                               .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR } },
        };

        fd.render_graph.add_pass(vsm_clear_pages)
            .add_pass(z_prepass)
            .add_pass(vsm_page_alloc)
            .add_pass(vsm_shadow)
            .add_pass(default_unlit)
            .add_pass(vsm_debug_page_copy)
            .add_pass(imgui)
            .add_pass(swapchain_present)
            .bake();
    }
}

void RendererVulkan::update() {
    if(flags.test(RenderFlags::PAUSE_RENDERING)) { return; }
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
        submit_queue->wait_idle();
        create_window_sized_resources();
        build_render_graph();
    }
    if(flags.test_clear(RenderFlags::UPDATE_BINDLESS_SET)) {
        assert(false);
        submit_queue->wait_idle();
        // update_bindless_set();
    }

    auto& fd = get_frame_data();
    const auto frame_num = Engine::get().frame_num();
    submit_queue->wait_fence(fd.fen_rendering_finished, -1ull); // todo: maybe wait here for 10secs and crash to desktop.
    fd.cmdpool->reset();

    uint32_t swapchain_index{};
    Image* swapchain_image{};
    {
        VkResult acquire_ret;
        swapchain_index = swapchain.acquire(&acquire_ret, ~0ull, fd.sem_swapchain);
        if(acquire_ret != VK_SUCCESS) {
            ENG_WARN("Acquire image failed with: {}", static_cast<uint32_t>(acquire_ret));
            return;
        }
        swapchain_image = &swapchain.images[swapchain_index];
    }

    submit_queue->reset_fence(get_frame_data().fen_rendering_finished);
    // vkResetFences(dev, 1, &get_frame_data().fen_rendering_finished.fence);

    {
        const float hx = (halton(Engine::get().frame_num() % 4u, 2) * 2.0 - 1.0);
        const float hy = (halton(Engine::get().frame_num() % 4u, 3) * 2.0 - 1.0);
        const glm::mat3 rand_mat =
            glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

        const auto ldir = glm::normalize(*(glm::vec3*)Engine::get().scene->debug_dir_light_dir);
        const auto lpos = *(glm::vec3*)Engine::get().scene->debug_dir_light_pos;
        // const auto ldir = glm::normalize(glm::vec3{ 1.0f, 0.0f, 0.0f });
        const auto eye = -ldir * 25.0f;
        // auto vsm_light_mat = glm::lookAt(eye, ldir, glm::vec3{ 0.0f, 1.0f, 0.0f });
        auto vsm_light_mat = glm::lookAt(lpos, glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f });
        const auto camdir = Engine::get().camera->pos - eye;
        const auto d = glm::dot(ldir, camdir);
        auto proj_pos = -glm::vec4{ camdir - d * ldir, 0.0f };
        proj_pos = vsm_light_mat * proj_pos;

        const auto dir_light_view = vsm_light_mat;
        // const auto dir_light_view = vsm_light_mat;
        const auto dir_light_proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 50.0f);
        // const auto dir_light_proj = glm::perspectiveFov(glm::radians(90.0f), 8.0f * 1024.0f, 8.0f * 1024.0f, 0.0f, 150.0f);

        GPUVsmConstantsBuffer vsmconsts{
            .dir_light_view = dir_light_view,
            .dir_light_proj = dir_light_proj,
            .dir_light_proj_view = dir_light_proj * dir_light_view,
            .dir_light_dir = ldir,
            .num_pages_xy = 64,
            .max_clipmap_index = 0,
            .texel_resolution = 8.0f * 1024.0f,
            .num_frags = 0,
        };

        GPUConstantsBuffer constants{
            .view = Engine::get().camera->get_view(),
            .proj = Engine::get().camera->get_projection(),
            .proj_view = Engine::get().camera->get_projection() * Engine::get().camera->get_view(),
            .inv_view = glm::inverse(Engine::get().camera->get_view()),
            .inv_proj = glm::inverse(Engine::get().camera->get_projection()),
            .inv_proj_view = glm::inverse(Engine::get().camera->get_projection() * Engine::get().camera->get_view()),
        };
        staging_buffer->send_to(fd.constants, 0ull, constants).send_to(vsm.constants_buffer, 0ull, vsmconsts).submit();
    }

    if(flags.test_clear(RenderFlags::DIRTY_TRANSFORMS_BIT)) { upload_transforms(); }
    const auto cmd = fd.cmdpool->begin();
    fd.render_graph.render(cmd, swapchain_index);
    fd.cmdpool->end(cmd);
    submit_queue->with_cmd_buf(cmd)
        .with_wait_sem(fd.sem_swapchain, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
        .with_sig_sem(fd.sem_rendering_finished, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
        .with_fence(fd.fen_rendering_finished)
        .submit();

    auto pinfo = Vks(VkPresentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &fd.sem_rendering_finished,
        .swapchainCount = 1,
        .pSwapchains = &swapchain.swapchain,
        .pImageIndices = &swapchain_index,
    });
    vkQueuePresentKHR(submit_queue->queue, &pinfo);
    if(!flags.empty()) { ENG_WARN("render flags not empty at the end of the frame: {:b}", flags.flags); }

    // flags.clear();
    submit_queue->wait_idle();
    return;
}

void RendererVulkan::on_window_resize() {
    flags.set(RenderFlags::RESIZE_SWAPCHAIN_BIT);
    set_screen(ScreenRect{ .w = Engine::get().window->width, .h = Engine::get().window->height });
}

void RendererVulkan::set_screen(ScreenRect screen) {
    assert(false);
    // screen_rect = screen;
    //  ENG_WARN("TODO: Resize resources on new set_screen()");
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
    const auto handle = make_image(desc.name, Vks(VkImageCreateInfo{ .imageType = deduce_image_type(desc.type),
                                                                     .format = deduce_image_format(desc.format),
                                                                     .extent = { desc.width, desc.height, 1u },
                                                                     .mipLevels = desc.mips,
                                                                     .arrayLayers = 1u,
                                                                     .samples = VK_SAMPLE_COUNT_1_BIT,
                                                                     .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT }));
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
                                                                        get_image(handle).get_view(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL)),
        .image = get_image(handle).image,
        .filter = filter,
        .addressing = addressing,
    };
    tex_ids.emplace(handle, id);
    return id.id;
}

Handle<Image> RendererVulkan::get_color_output_texture() const { return get_frame_data().gbuffer.color_image; }

RenderMaterial RendererVulkan::get_material(Handle<RenderMaterial> handle) const {
    if(!handle) { return RenderMaterial{}; }
    return materials.at(handle);
}

VsmData& RendererVulkan::get_vsm_data() { return vsm; }

void RendererVulkan::upload_model_textures() {
    for(auto& tex : upload_images) {
        Image& img = get_image(tex.image_handle);
        // VkBufferImageCopy copy{
        //     .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0,
        //     .layerCount = 1 }, .imageOffset = {}, .imageExtent = img.extent,
        // };
        // staging_buffer->send_to(img, std::as_bytes(std::span{ tex.rgba_data }), copy);
        staging_buffer->send_to(tex.image_handle, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, {}, tex.rgba_data);
    }
    staging_buffer->submit();
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
    staging_buffer->send_to(vertex_positions_buffer, ~0ull, positions)
        .send_to(vertex_attributes_buffer, ~0ull, attributes)
        .send_to(index_buffer, ~0ull, upload_indices)
        .submit();
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
            .color_texture_idx = get_bindless_index(mat.textures.base_color_texture.handle, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                    samplers.get_sampler(mat.textures.base_color_texture.filter,
                                                                         mat.textures.base_color_texture.addressing)),
            .normal_texture_idx = get_bindless_index(mat.textures.normal_texture.handle, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                     samplers.get_sampler(mat.textures.normal_texture.filter,
                                                                          mat.textures.normal_texture.addressing)),
            .metallic_roughness_idx =
                get_bindless_index(mat.textures.metallic_roughness_texture.handle, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
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
        indirect_draw_buffer = make_buffer("indirect draw", 
            Vks(VkBufferCreateInfo{
                    .size = sizeof(IndirectDrawCommandBufferHeader) + gpu_draw_commands.size() * sizeof(gpu_draw_commands[0]),
                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                }), VmaAllocationCreateInfo{});
    }
    staging_buffer->send_to(indirect_draw_buffer, 0ull, gpu_draw_header).submit();
    staging_buffer->send_to(indirect_draw_buffer, ~0ull, gpu_draw_commands).submit();

    if(!mesh_instances_buffer) {
        mesh_instances_buffer = make_buffer("mesh instances", 
            Vks(VkBufferCreateInfo{
                    .size = gpu_mesh_instances.size() * sizeof(gpu_mesh_instances[0]),
                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                }), VmaAllocationCreateInfo{});
    }
    staging_buffer->send_to(mesh_instances_buffer, 0ull, gpu_mesh_instances).submit();
    // clang-format on
}

void RendererVulkan::upload_transforms() {
    std::swap(get_frame_data().transform_buffers, get_frame_data(1).transform_buffers);
    Buffer& dst_transforms = get_buffer(get_frame_data().transform_buffers);
    std::vector<glm::mat4> transforms;
    transforms.reserve(mesh_instances.size());
    for(auto e : mesh_instances) {
        transforms.push_back(Engine::get().ecs_storage->get<components::Transform>(e).transform);
    }
    staging_buffer->send_to(get_frame_data().transform_buffers, 0ull, transforms).submit();
}

void RendererVulkan::build_blas() {
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
#endif
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
    submit_queue->wait_idle();
#endif
}

Handle<Buffer> RendererVulkan::make_buffer(const std::string& name, const VkBufferCreateInfo& vk_info,
                                           const VmaAllocationCreateInfo& vma_info) {
    const auto handle =
        buffers.emplace(Handle<Buffer>{ generate_handle }, Buffer{ name, dev, vma, vk_info, vma_info }).first->first;
    bindless_pool->register_buffer(handle);
    return handle;
}

Handle<Buffer> RendererVulkan::make_buffer(const std::string& name, buffer_resizable_t resizable,
                                           const VkBufferCreateInfo& vk_info, const VmaAllocationCreateInfo& vma_info) {
    const auto handle =
        buffers.emplace(Handle<Buffer>{ generate_handle }, Buffer{ name, dev, vma, resizable, vk_info, vma_info })
            .first->first;
    bindless_pool->register_buffer(handle);
    return handle;
}

Handle<Image> RendererVulkan::make_image(const std::string& name, const VkImageCreateInfo& vk_info) {
    const auto handle = images.emplace(Handle<Image>{ generate_handle }, Image{ name, dev, vma, vk_info }).first->first;
    return handle;
}

VkImageView RendererVulkan::make_image_view(Handle<Image> handle, VkImageLayout layout, VkSampler sampler) {
    const auto view = get_image(handle).get_view();
    bindless_pool->register_image_view(view, layout, sampler);
    return view;
}

VkImageView RendererVulkan::make_image_view(Handle<Image> handle, const VkImageViewCreateInfo& vk_info,
                                            VkImageLayout layout, VkSampler sampler) {
    const auto view = get_image(handle).get_view(vk_info);
    bindless_pool->register_image_view(view, layout, sampler);
    return view;
}

Buffer& RendererVulkan::get_buffer(Handle<Buffer> handle) { return buffers.at(handle); }

Image& RendererVulkan::get_image(Handle<Image> handle) { return images.at(handle); }

void RendererVulkan::destroy_buffer(Handle<Buffer> buffer) {
    auto& b = get_buffer(buffer);
    vmaDestroyBuffer(b.vma, b.buffer, b.alloc);
    b = Buffer{};
}

void RendererVulkan::replace_buffer(Handle<Buffer> dst_buffer, Buffer&& src_buffer) {
    destroy_buffer(dst_buffer);
    buffers.at(dst_buffer) = std::move(src_buffer);
    bindless_pool->update_bindless_resource(dst_buffer);
}

uint32_t RendererVulkan::register_bindless_index(Handle<Buffer> handle) {
    return bindless_pool->register_buffer(handle);
}

uint32_t RendererVulkan::register_bindless_index(VkImageView view, VkImageLayout layout, VkSampler sampler) {
    return bindless_pool->register_image_view(view, layout, sampler);
}

uint32_t RendererVulkan::get_bindless_index(Handle<Buffer> handle) { return bindless_pool->get_bindless_index(handle); }

uint32_t RendererVulkan::get_bindless_index(Handle<Image> handle, VkImageLayout layout, VkSampler sampler) {
    if(!handle) { return -1ull; }
    for(const auto& e : image_view_storage.configs[handle]) {
        if(e.layout == layout && e.sampler == sampler) { return get_bindless_index(e.view); }
    }
    auto view = make_image_view(handle, layout, sampler);
    image_view_storage.configs[handle].push_back(ImageViewStorage::Config{ view, layout, sampler });
    return get_bindless_index(view);
}

uint32_t RendererVulkan::get_bindless_index(VkImageView view) { return bindless_pool->get_bindless_index(view); }

FrameData& RendererVulkan::get_frame_data(uint32_t offset) {
    return frame_datas[(Engine::get().frame_num() + offset) % frame_datas.size()];
}

const FrameData& RendererVulkan::get_frame_data(uint32_t offset) const {
    return const_cast<RendererVulkan*>(this)->get_frame_data();
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
        std::string path_to_includes = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders").string();
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
    VK_CHECK(vkCreateShaderModule(RendererVulkan::get_instance()->dev, &module_info, nullptr, &mod));
    return mod;
}

void ShaderStorage::canonize_path(std::filesystem::path& p) {
    static const auto prefix = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders");
    if(!p.string().starts_with(prefix.string())) { p = prefix / p; }
    p.make_preferred();
}

void Swapchain::create(VkDevice dev, uint32_t image_count, uint32_t width, uint32_t height) {
    auto sinfo = Vks(VkSwapchainCreateInfoKHR{
        // sinfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        // sinfo.pNext = &format_list_info;
        .surface = RendererVulkan::get_instance()->window_surface,
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

    if(swapchain) { vkDestroySwapchainKHR(dev, swapchain, nullptr); }
    VK_CHECK(vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain));
    std::vector<VkImage> vk_images(image_count);
    images.resize(image_count);
    views.resize(image_count);

    VK_CHECK(vkGetSwapchainImagesKHR(dev, swapchain, &image_count, vk_images.data()));

    for(uint32_t i = 0; i < image_count; ++i) {
        images[i] =
            Image{ std::format("swpachain_image_{}", i), dev, vk_images[i],
                   Vks(VkImageCreateInfo{
                       .imageType = VK_IMAGE_TYPE_2D,
                       .format = sinfo.imageFormat,
                       .extent = VkExtent3D{ .width = sinfo.imageExtent.width, .height = sinfo.imageExtent.height, .depth = 1u },
                       .mipLevels = 1,
                       .arrayLayers = 1,
                       .usage = sinfo.imageUsage }) };
        views[i] = images[i].get_view();
    }
}

uint32_t Swapchain::acquire(VkResult* res, uint64_t timeout, VkSemaphore semaphore, VkFence fence) {
    uint32_t idx;
    auto result = vkAcquireNextImageKHR(RendererVulkan::get_instance()->dev, swapchain, timeout, semaphore, fence, &idx);
    if(res) { *res = result; }
    return idx;
}

rendergraph::RenderGraph& rendergraph::RenderGraph::add_pass(RenderPass* pass) {
    const auto type = pass->shaders.empty() ? VK_SHADER_STAGE_VERTEX_BIT
                                            : RendererVulkan::get_instance()->shader_storage.get_stage(pass->shaders.front());
    if(!pass->shaders.empty()) {
        if(type & (VK_SHADER_STAGE_VERTEX_BIT)) {
            pass->pipeline_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
            if(pass->pipeline_settings.valueless_by_exception()) { pass->pipeline_settings = RasterizationSettings{}; }
        } else if(type & (VK_SHADER_STAGE_COMPUTE_BIT)) {
            pass->pipeline_bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        } else if(type & (VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR)) {
            pass->pipeline_bind_point = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
            if(pass->pipeline_settings.valueless_by_exception()) {
                ENG_WARN("Raytracing pipeline pass does not have defined settings. Not adding...");
                return *this;
            }
        }
    }
    render_list.push_back(pass);
    return *this;
}

rendergraph::RenderGraph::RenderGraph(VkDevice dev) noexcept : dev(dev) {}

rendergraph::RenderPass* rendergraph::RenderGraph::make_pass() { return &passes.emplace_back(RenderPass{}); }

void rendergraph::RenderGraph::bake() {
#if 0
    struct Barrier {
        VkPipelineStageFlags2 src_stage{}, dst_stage{};
        VkAccessFlags2 src_access{}, dst_access{};
        VkImageLayout src_layout{}, dst_layout{};
    };
    struct LastAccess {
        int32_t first_read{ INT32_MAX };
        int32_t first_write{ INT32_MAX };
        int32_t last_read{ -1 };
        int32_t last_write{ -1 };
        Barrier last_barrier{};
    };
    std::map<decltype(Access::resource), LastAccess> accesses;
#endif

    stages.clear();
    stages.reserve(passes.size());

    // TODO: Maybe multithread this later (shaders for now are all precompiled)
    for(auto& p : passes) {
        if(!p.pipeline) { create_pipeline(p); }
    }

    // access history for each resource
    struct Accesses {
        int32_t first_read{ INT32_MAX };
        int32_t first_write{ INT32_MAX };
        int32_t last_read{ -1 };
        int32_t last_write{ -1 };
        std::vector<Access*> accesses;
    };
    std::map<decltype(Access::resource), Accesses> accesses;
    const auto get_stage = [this](uint32_t idx) -> decltype(auto) {
        if(stages.size() <= idx) { stages.resize(idx + 1); }
        return stages.at(idx);
    };
    auto& r = *RendererVulkan::get_instance();

    for(auto i = 0u; i < render_list.size(); ++i) {
        auto& p = *render_list.at(i);
        uint32_t stage_idx = 0;
        Stage stage;

        // 1. find resource used in the latest stage in the graph render list and make the stage after that it's
        // pass's stage (pass can only happen after the latest stage modifying one of it's resources)
        // 2. generate appropiate barriers
        // 3. append to access history
        for(auto& a : p.accesses) {
            auto& acc = accesses[a.resource];
            int32_t a_stage = 0;
            if((a.type & AccessType::WRITE_BIT) || (a.type == AccessType::NONE_BIT)) {
                a_stage = std::max(acc.last_read, acc.last_write) + 1;
            } else if(a.type & AccessType::READ_BIT) {
                a_stage = std::max(acc.last_write,
                                   (acc.last_read > -1 && acc.accesses.back()->layout != a.layout) ? acc.last_read : acc.last_write) +
                          1;
            } else {
                ENG_WARN("Unrecognized Access type. Skipping.");
                assert(false);
                continue;
            }
            stage_idx = std::max(stage_idx, (uint32_t)std::max(0, a_stage));

            if(auto* handle = std::get_if<Handle<Buffer>>(&a.resource)) {
                stage.buffer_barriers.push_back(Vks(VkBufferMemoryBarrier2{
                    .srcStageMask = acc.accesses.empty() ? VK_PIPELINE_STAGE_2_NONE : acc.accesses.back()->stage,
                    .srcAccessMask = acc.accesses.empty() ? VK_ACCESS_2_NONE : acc.accesses.back()->access,
                    .dstStageMask = a.stage,
                    .dstAccessMask = a.access,
                    .buffer = r.get_buffer(*handle).buffer,
                    .offset = 0ull,
                    .size = VK_WHOLE_SIZE }));
            } else if(auto* handle = std::get_if<Handle<Image>>(&a.resource)) {
                if(a.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT) {
                    assert(!stage.get_swapchain_image_callback);
                    stage.get_swapchain_image_callback = [](uint32_t index) -> Image& {
                        return RendererVulkan::get_instance()->swapchain.images.at(index);
                    };
                }
                stage.image_barriers.push_back(Vks(VkImageMemoryBarrier2{
                    .srcStageMask = acc.accesses.empty() ? VK_PIPELINE_STAGE_2_NONE : acc.accesses.back()->stage,
                    .srcAccessMask = acc.accesses.empty() ? VK_ACCESS_2_NONE : acc.accesses.back()->access,
                    .dstStageMask = a.stage,
                    .dstAccessMask = a.access,
                    .oldLayout = (a.flags & ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT) ? VK_IMAGE_LAYOUT_UNDEFINED
                                 : acc.accesses.empty() ? (*handle ? r.get_image(*handle).current_layout : VK_IMAGE_LAYOUT_UNDEFINED)
                                                        : acc.accesses.back()->layout,
                    .newLayout = a.layout,
                    .image = *handle ? r.get_image(*handle).image : VkImage{},
                    .subresourceRange = { .aspectMask = *handle ? r.get_image(*handle).deduce_aspect() : VK_IMAGE_ASPECT_COLOR_BIT,
                                          .levelCount = VK_REMAINING_MIP_LEVELS,
                                          .layerCount = VK_REMAINING_ARRAY_LAYERS } }));
            } else {
                ENG_WARN("Unhandled resource type");
                assert(false);
                continue;
            }
            acc.accesses.push_back(&a);
        }

        // 1. update access history
        for(auto& a : p.accesses) {
            if(a.type & AccessType::READ_BIT) {
                accesses.at(a.resource).first_read = std::min(accesses.at(a.resource).first_read, (int32_t)stage_idx);
                accesses.at(a.resource).last_read = stage_idx;
            }
            if(a.type & AccessType::WRITE_BIT) {
                accesses.at(a.resource).first_write = std::min(accesses.at(a.resource).first_write, (int32_t)stage_idx);
                accesses.at(a.resource).last_write = stage_idx;
            }
        }

        // find actual stage, having considered all previous accesses to specified in this pass resources.
        Stage& dst_stage = get_stage(stage_idx);
        dst_stage.get_swapchain_image_callback = stage.get_swapchain_image_callback;
        dst_stage.buffer_barriers.insert(dst_stage.buffer_barriers.end(), stage.buffer_barriers.begin(),
                                         stage.buffer_barriers.end());
        dst_stage.image_barriers.insert(dst_stage.image_barriers.end(), stage.image_barriers.begin(),
                                        stage.image_barriers.end());
        dst_stage.passes.push_back(&p);
    }

    // 1. update oldLayout in first barrier of each image resource to newLayout of it's last barrier, to complete the rendering cycle
    // 2. transition pre-rendergraph layouts of images to point 1.
    std::vector<VkImageMemoryBarrier2> initial_barriers;
    for(auto& a : accesses) {
        if(!std::holds_alternative<Handle<Image>>(a.first)) { continue; }
        const auto handle = std::get<Handle<Image>>(a.first);
        const auto first_stage_index = std::min(a.second.first_read, a.second.first_write);
        assert(first_stage_index < stages.size());
        auto& first_stage = get_stage(first_stage_index);
        auto& first_barrier =
            *std::find_if(first_stage.image_barriers.begin(), first_stage.image_barriers.end(), [&a, &handle](const auto& b) {
                if(!handle) { return b.image == nullptr; }
                return b.image == RendererVulkan::get_instance()->get_image(handle).image;
            });
        const auto& last_barrier = a.second.accesses.back();
        if(a.second.accesses.front()->flags & ResourceFlags::FROM_UNDEFINED_LAYOUT_BIT) { continue; }
        initial_barriers.push_back(Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                                                              .srcAccessMask = VK_ACCESS_2_NONE,
                                                              .dstStageMask = first_barrier.dstStageMask,
                                                              .dstAccessMask = first_barrier.dstAccessMask,
                                                              .oldLayout = first_barrier.oldLayout,
                                                              .newLayout = last_barrier->layout,
                                                              .image = first_barrier.image,
                                                              .subresourceRange = first_barrier.subresourceRange }));
        first_barrier.oldLayout = last_barrier->layout;
        if(!first_barrier.image) {
            // if it's swapchain image, they'll all need the same starting layout, so just make them be so here.
            initial_barriers.back().image = first_stage.get_swapchain_image_callback(0).image;
            first_stage.get_swapchain_image_callback(0).current_layout = initial_barriers.back().newLayout;
            for(uint32_t i = 1; i < RendererVulkan::get_instance()->swapchain.images.size(); ++i) {
                initial_barriers.push_back(initial_barriers.back());
                initial_barriers.back().image = first_stage.get_swapchain_image_callback(i).image;
                first_stage.get_swapchain_image_callback(i).current_layout = initial_barriers.back().newLayout;
            }
        } else {
            r.get_image(handle).current_layout = initial_barriers.back().newLayout;
        }
    }
    auto initial_dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = static_cast<uint32_t>(initial_barriers.size()),
                                                  .pImageMemoryBarriers = initial_barriers.data() });
    auto cmd = r.get_frame_data().cmdpool->begin();
    vkCmdPipelineBarrier2(cmd, &initial_dep_info);
    r.get_frame_data().cmdpool->end(cmd);
    r.submit_queue->with_cmd_buf(cmd).submit_wait(-1ull);

    // make all the swapchain barriers with image == nullptr go to the front for
    // easy replacing.
    for(auto& p : stages) {
        if(!p.get_swapchain_image_callback) { continue; }
        for(auto i = 0u; i < p.image_barriers.size(); ++i) {
            if(!p.image_barriers.at(i).image) {
                std::swap(p.image_barriers.at(0), p.image_barriers.at(i));
                break;
            }
        }
    }

#if 0
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
                    assert(!s.get_swapchain_image_callback);
                    s.get_swapchain_image_callback = [la](uint32_t index) {
                        return RendererVulkan::get_instance()->swapchain.images.at(index).image;
                    };
                }
                const auto* img = a.resource.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT
                                      ? nullptr
                                      : &RendererVulkan::get_instance()->images.at(a.resource.resource_idx);
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
                    .buffer = RendererVulkan::get_instance()->buffers.at(a.resource.resource_idx).buffer,
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
                                     : RendererVulkan::get_instance()->images.at(a.first.resource_idx).image](const auto& b) {
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
        if(stages.at(first_stage).get_swapchain_image_callback && !first_barrier.image) {
            // if it's swapchain image, they'll all need the same starting layout, so just make them be so here.
            initial_barriers.back().image = stages.at(first_stage).get_swapchain_image_callback(0);
            for(uint32_t i = 1; i < RendererVulkan::get_instance()->swapchain.images.size(); ++i) {
                initial_barriers.push_back(initial_barriers.back());
                initial_barriers.back().image = stages.at(first_stage).get_swapchain_image_callback(i);
            }
        }
    }
    auto initial_dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = static_cast<uint32_t>(initial_barriers.size()),
                                                  .pImageMemoryBarriers = initial_barriers.data() });
    auto cmd = RendererVulkan::get_instance()->get_frame_data().cmdpool->begin_onetime();
    vkCmdPipelineBarrier2(cmd, &initial_dep_info);
    RendererVulkan::get_instance()->get_frame_data().cmdpool->end(cmd);
    Fence f{ RendererVulkan::get_instance()->dev, false };
    RendererVulkan::get_instance()->gq.submit(cmd, &f);

    // make all the swapchain barriers with image == nullptr go to the front for
    // easy replacing.
    for(auto& p : stages) {
        if(!p.get_swapchain_image_callback) { continue; }
        for(uint32_t i = 0; i < p.image_barriers.size(); ++i) {
            if(!p.image_barriers.at(i).image) {
                std::swap(p.image_barriers.at(0), p.image_barriers.at(i));
                break;
            }
        }
    }

    f.wait();
#endif
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
        if(pass.pipeline && !pass.name.empty()) {
            set_debug_name(pass.pipeline->pipeline, std::format("{}_render_pass_pipeline", pass.name));
        }
        return;
    }

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for(const auto& p : pass.shaders) {
        auto stage = Vks(VkPipelineShaderStageCreateInfo{
            .stage = RendererVulkan::get_instance()->shader_storage.get_stage(p),
            .module = RendererVulkan::get_instance()->shader_storage.get_shader(p),
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
        VK_CHECK(vkCreateGraphicsPipelines(RendererVulkan::get_instance()->dev, nullptr, 1, &vk_info, nullptr, &pipeline));
    } else if(pass.pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
        assert(stages.size() == 1);
        auto vk_info = Vks(VkComputePipelineCreateInfo{
            .stage = stages.at(0),
            .layout = RendererVulkan::get_instance()->bindless_pool->get_pipeline_layout(),
        });
        VK_CHECK(vkCreateComputePipelines(RendererVulkan::get_instance()->dev, nullptr, 1, &vk_info, nullptr, &pipeline));
    } else if(pass.pipeline_bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
        auto& settings = *std::get_if<RaytracingSettings>(&pass.pipeline_settings);
        auto vk_info = Vks(VkRayTracingPipelineCreateInfoKHR{
            .stageCount = (uint32_t)stages.size(),
            .pStages = stages.data(),
            .groupCount = (uint32_t)settings.groups.size(),
            .pGroups = settings.groups.data(),
            .maxPipelineRayRecursionDepth = settings.recursion_depth,
            .layout = RendererVulkan::get_instance()->bindless_pool->get_pipeline_layout(),
        });
        VK_CHECK(vkCreateRayTracingPipelinesKHR(RendererVulkan::get_instance()->dev, nullptr, nullptr, 1, &vk_info, nullptr, &pipeline));

        const uint32_t handleSize = RendererVulkan::get_instance()->rt_props.shaderGroupHandleSize;
        const uint32_t handleSizeAligned = align_up(handleSize, RendererVulkan::get_instance()->rt_props.shaderGroupHandleAlignment);
        const uint32_t groupCount = static_cast<uint32_t>(settings.groups.size());
        const uint32_t sbtSize = groupCount * handleSizeAligned;

        std::vector<uint8_t> shaderHandleStorage(sbtSize);
        vkGetRayTracingShaderGroupHandlesKHR(RendererVulkan::get_instance()->dev, pipeline, 0, groupCount, sbtSize,
                                             shaderHandleStorage.data());

        const VkBufferUsageFlags bufferUsageFlags =
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        settings.sbt_buffer =
            RendererVulkan::get_instance()->make_buffer("buffer_sbt",
                                                        Vks(VkBufferCreateInfo{ .size = sbtSize, .usage = bufferUsageFlags }),
                                                        VmaAllocationCreateInfo{});
        RendererVulkan::get_instance()->staging_buffer->send_to(settings.sbt_buffer, 0ull, shaderHandleStorage).submit();
    } else {
        assert(false);
    }
    pipelines.push_back(Pipeline{ .pipeline = pipeline });
    pass.pipeline = &pipelines.back();
}

void rendergraph::RenderGraph::render(VkCommandBuffer cmd, uint32_t swapchain_index) {
    for(auto& s : stages) {
        if(s.get_swapchain_image_callback) {
            s.image_barriers.at(0).image = s.get_swapchain_image_callback(swapchain_index).image;
        }
        auto dep_info = Vks(VkDependencyInfo{
            .bufferMemoryBarrierCount = static_cast<uint32_t>(s.buffer_barriers.size()),
            .pBufferMemoryBarriers = s.buffer_barriers.data(),
            .imageMemoryBarrierCount = static_cast<uint32_t>(s.image_barriers.size()),
            .pImageMemoryBarriers = s.image_barriers.data(),
        });
        vkCmdPipelineBarrier2(cmd, &dep_info);
        for(auto p : s.passes) {
            auto& pass = *p;
            if(pass.pipeline) { vkCmdBindPipeline(cmd, pass.pipeline_bind_point, pass.pipeline->pipeline); }
            if(pass.callback_render) { pass.callback_render(cmd, swapchain_index, pass); }
        }
    }
}