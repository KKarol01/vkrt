#include <filesystem>
#include <bitset>
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
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <eng/engine.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/utils.hpp>
#include <assets/shaders/bindless_structures.inc>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/descpool.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/passes/passes.hpp>

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
                        .add_desired_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
                        .add_desired_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)
                        .add_desired_extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
                        .add_desired_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
                        .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)        // for imgui
                        .add_required_extension(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) // for imgui
                        .prefer_gpu_device_type()
                        .require_present()
                        .select();
    if(!phys_ret) { throw std::runtime_error{ "Failed to select Vulkan Physical Device. Error: " }; }

    supports_raytracing = phys_ret->is_extension_present(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                          phys_ret->is_extension_present(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

    auto ray_query = phys_ret->is_extension_present(VK_KHR_RAY_QUERY_EXTENSION_NAME);

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
    vkb::DeviceBuilder device_builder{ phys_ret.value() };
    device_builder.add_pNext(&dev_2_features).add_pNext(&dyn_features).add_pNext(&synch2_features).add_pNext(&dev_vk12_features);
    if(supports_raytracing) {
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

    vsm.dir_light_page_table =
        make_image("vsm dir light 0 page table",
                   VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                      .format = VK_FORMAT_R32_SFLOAT,
                                      .extent = { vsm_constants.num_pages_xy, vsm_constants.num_pages_xy, 1 },
                                      .mipLevels = 1,
                                      .arrayLayers = 4,
                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                      .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT });

    vsm.dir_light_page_table_rgb8 =
        make_image("vsm dir light 0 page table rgb8",
                   VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
                                      .format = VK_FORMAT_R8G8B8A8_UNORM,
                                      .extent = { vsm_constants.num_pages_xy, vsm_constants.num_pages_xy, 1 },
                                      .mipLevels = 1,
                                      .arrayLayers = 4,
                                      .samples = VK_SAMPLE_COUNT_1_BIT,
                                      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });

    create_window_sized_resources();

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
    using namespace rendergraph2;
    rendergraph.clear_passes();
    rendergraph.add_pass<ZPrepassPass>(&rendergraph);
    rendergraph.add_pass<VsmClearPagesPass>(&rendergraph);
    rendergraph.add_pass<VsmPageAllocPass>(&rendergraph);
    rendergraph.add_pass<VsmShadowsPass>(&rendergraph);
    rendergraph.add_pass<VsmDebugPageCopyPass>(&rendergraph);
    rendergraph.add_pass<DefaultUnlitPass>(&rendergraph);
    rendergraph.add_pass<ImguiPass>(&rendergraph);
    rendergraph.add_pass<SwapchainPresentPass>(&rendergraph);
    rendergraph.bake();
    int x = 0;
#if 0
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
#endif
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

    pipelines.threaded_compile();

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
    rendergraph.render(cmd);
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
            .color_texture_idx = get_bindless_index(get_image(mat.textures.base_color_texture.handle).get_view(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                    samplers.get_sampler(mat.textures.base_color_texture.filter,
                                                                         mat.textures.base_color_texture.addressing)),
            .normal_texture_idx = mat.textures.normal_texture.handle
                                      ? get_bindless_index(get_image(mat.textures.normal_texture.handle).get_view(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                                           samplers.get_sampler(mat.textures.normal_texture.filter,
                                                                                mat.textures.normal_texture.addressing))
                                      : ~0ul,
            .metallic_roughness_idx =
                mat.textures.metallic_roughness_texture.handle
                    ? get_bindless_index(get_image(mat.textures.metallic_roughness_texture.handle).get_view(), VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                         samplers.get_sampler(mat.textures.metallic_roughness_texture.filter,
                                                              mat.textures.metallic_roughness_texture.addressing))
                    : ~0ul,
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
    staging_buffer->send_to(get_frame_data().transform_buffers, 0ull, transforms)
        .send_to(get_frame_data(1).transform_buffers, 0ull, transforms)
        .submit();
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
    return handle;
}

Handle<Buffer> RendererVulkan::make_buffer(const std::string& name, buffer_resizable_t resizable,
                                           const VkBufferCreateInfo& vk_info, const VmaAllocationCreateInfo& vma_info) {
    const auto handle =
        buffers.emplace(Handle<Buffer>{ generate_handle }, Buffer{ name, dev, vma, resizable, vk_info, vma_info })
            .first->first;
    return handle;
}

Handle<Image> RendererVulkan::make_image(const std::string& name, const VkImageCreateInfo& vk_info) {
    const auto handle = images.emplace(Handle<Image>{ generate_handle }, Image{ name, dev, vma, vk_info }).first->first;
    return handle;
}

VkImageView RendererVulkan::make_image_view(Handle<Image> handle) {
    return make_image_view(handle, Vks(VkImageViewCreateInfo{}));
}

VkImageView RendererVulkan::make_image_view(Handle<Image> handle, const VkImageViewCreateInfo& vk_info) {
    return get_image(handle).get_view(vk_info);
}

Buffer& RendererVulkan::get_buffer(Handle<Buffer> handle) { return get_instance()->buffers.at(handle); }

Image& RendererVulkan::get_image(Handle<Image> handle) { return get_instance()->images.at(handle); }

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

uint32_t RendererVulkan::get_bindless_index(Handle<Buffer> handle) { return bindless_pool->get_bindless_index(handle); }

uint32_t RendererVulkan::get_bindless_index(VkImageView view, VkImageLayout layout, VkSampler sampler) {
    return bindless_pool->get_bindless_index(view, layout, sampler);
}

FrameData& RendererVulkan::get_frame_data(uint32_t offset) {
    return frame_datas[(Engine::get().frame_num() + offset) % frame_datas.size()];
}

const FrameData& RendererVulkan::get_frame_data(uint32_t offset) const {
    return const_cast<RendererVulkan*>(this)->get_frame_data();
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
            Image{ std::format("swapchain_image_{}", i), dev, vk_images[i],
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
    current_index = idx;
    return idx;
}

Image& Swapchain::get_current_image() { return images.at(current_index); }

VkImageView& Swapchain::get_current_view() { return views.at(current_index); }
