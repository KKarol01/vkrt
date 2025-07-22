#include <filesystem>
#include <bitset>
#include <numeric>
#include <fstream>
#include <utility>
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
#include <eng/common/to_vk.hpp>
#include <eng/common/paths.hpp>

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

namespace gfx
{

RendererVulkan* RendererVulkan::get_instance() { return static_cast<RendererVulkan*>(Engine::get().renderer); }

void RendererVulkan::init()
{
    initialize_vulkan();
    initialize_resources();
    initialize_materials();
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
    IMGUI_CHECKVERSION();
    void* user_data;
    ImGui::CreateContext();
    // ImGui::GetAllocatorFunctions(&Engine::get().ui_ctx->alloc_callbacks->imgui_alloc,
    //                              &Engine::get().ui_ctx->alloc_callbacks->imgui_free, &user_data);
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Engine::get().window->window, true);

    VkFormat color_formats[]{ swapchain.images.at(0).format };

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
    submit_queue->with_cmd_buf(cmdimgui).submit_wait(~0ull);

    // Engine::get().ui->add_tab("FFTOcean", [] {
    //     auto* r = RendererVulkan::get_instance();
    //     auto& fftc = r->fftocean.recalc_state_0;
    //     fftc |= ImGui::SliderFloat("patch size", &r->fftocean.settings.patch_size, 0.1f, 50.0f);
    //     fftc |= ImGui::SliderFloat2("wind dir", &r->fftocean.settings.wind_dir.x, -100.0f, 100.0f);
    //     fftc |= ImGui::SliderFloat("phillips const", &r->fftocean.settings.phillips_const, 0.01, 10.0f);
    //     ImGui::SliderFloat("time speed", &r->fftocean.settings.time_speed, 0.01f, 10.0f);
    //     ImGui::SliderFloat("lambda", &r->fftocean.settings.disp_lambda, -10.0f, 10.0f);
    //     fftc |= ImGui::SliderFloat("small l", &r->fftocean.settings.small_l, 0.001, 2.0f);
    // });
}

void RendererVulkan::initialize_resources()
{
    bindless_pool = new BindlessPool{ dev };
    staging_buffer = new StagingBuffer{ submit_queue };
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

    geom_main_bufs.buf_vpos = make_buffer(BufferCreateInfo{ "vertex positions", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    geom_main_bufs.buf_vattrs = make_buffer(BufferCreateInfo{ "vertex attributes", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    geom_main_bufs.buf_indices =
        make_buffer(BufferCreateInfo{ "vertex indices", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT });
    geom_main_bufs.buf_draw_cmds = make_buffer(BufferCreateInfo{
        "meshlets draw cmds", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, true });
    geom_main_bufs.buf_draw_ids =
        make_buffer(BufferCreateInfo{ "meshlets instance id", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    geom_main_bufs.buf_final_draw_ids =
        make_buffer(BufferCreateInfo{ "meshlets final instance id", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
    geom_main_bufs.buf_draw_bs =
        make_buffer(BufferCreateInfo{ "meshlets instance bbs", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });

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

    for(uint32_t i = 0; i < frame_datas.size(); ++i)
    {
        auto& fd = frame_datas[i];
        fd.cmdpool = submit_queue->make_command_pool();
        fd.sem_swapchain = submit_queue->make_semaphore();
        fd.sem_rendering_finished = submit_queue->make_semaphore();
        fd.fen_rendering_finished = submit_queue->make_fence(true);
        fd.constants = make_buffer(BufferCreateInfo{ fmt::format("constants_{}", i), 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
        fd.transform_buffers =
            make_buffer(BufferCreateInfo{ fmt::format("transform_buffer_{}", i), 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
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
        .shaders = { make_shader(Shader::Stage::COMPUTE, "culling/culling.comp.glsl") } });
    hiz_pipeline = make_pipeline(PipelineCreateInfo{
        .shaders = { make_shader(Shader::Stage::COMPUTE, "culling/hiz.comp.glsl") } });
    hiz_sampler = reinterpret_cast<VkSampler>(*batch_sampler(SamplerDescriptor{
        .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
        .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE },
        .mipmap_mode = SamplerDescriptor::MipMapMode::NEAREST,
        .reduction_mode = SamplerDescriptor::ReductionMode::MIN }));

    const auto pp_default_unlit = make_pipeline(PipelineCreateInfo{
        .depth_format = ImageFormat::D32_SFLOAT,
        .culling = PipelineCreateInfo::CullMode::BACK,
        .shaders = { make_shader(Shader::Stage::VERTEX, "default_unlit/unlit.vert.glsl"),
                     make_shader(Shader::Stage::PIXEL, "default_unlit/unlit.frag.glsl") },
        .depth_test = true,
        .depth_write = true,
        .depth_compare = PipelineCreateInfo::DepthCompare::GREATER,
    });
    MeshPassCreateInfo info{ .name = "default_unlit" };
    info.effects[(uint32_t)MeshPassType::FORWARD] = make_shader_effect(ShaderEffect{ .pipeline = pp_default_unlit });
    make_mesh_pass(info);
}

void RendererVulkan::initialize_materials() { default_material = materials.insert(Material{}).first; }

void RendererVulkan::create_window_sized_resources()
{
    swapchain.create(dev, frame_datas.size(), Engine::get().window->width, Engine::get().window->height);

    for(auto i = 0ull; i < frame_datas.size(); ++i)
    {
        auto& fd = frame_datas.at(i);

        fd.hiz_pyramid = make_image(ImageCreateInfo{
            .name = fmt::format("hiz_pyramid_{}", i),
            .type = VK_IMAGE_TYPE_2D,
            .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height, 1 },
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            .mips = (uint32_t)std::log2f(std::max(Engine::get().window->width, Engine::get().window->height)) + 1 });
        fd.hiz_debug_output = make_image(ImageCreateInfo{
            .name = fmt::format("hiz_debug_output_{}", i),
            .type = VK_IMAGE_TYPE_2D,
            .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height, 1 },
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            .mips = 1 });

        fd.gbuffer.color_image = make_image(ImageCreateInfo{
            .name = fmt::format("g_color_{}", i),
            .type = VK_IMAGE_TYPE_2D,
            .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height, 1 },
            .format = VK_FORMAT_R8G8B8A8_SRGB,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        fd.gbuffer.depth_buffer_image = make_image(ImageCreateInfo{
            .name = fmt::format("g_depth_{}", i),
            .type = VK_IMAGE_TYPE_2D,
            .extent = { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height, 1 },
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });

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

    static constexpr auto insert_vk_img_barrier = [](VkCommandBuffer cmd, Image& img, VkPipelineStageFlags2 src_stage,
                                                     VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                                                     VkAccessFlags2 dst_access, VkImageLayout old_layout, VkImageLayout new_layout) {
        const auto barr = Vks(VkImageMemoryBarrier2{ .srcStageMask = src_stage,
                                                     .srcAccessMask = src_access,
                                                     .dstStageMask = dst_stage,
                                                     .dstAccessMask = dst_access,
                                                     .oldLayout = old_layout,
                                                     .newLayout = new_layout,
                                                     .image = img.image,
                                                     .subresourceRange = { img.deduce_aspect(), 0, img.mips, 0, img.layers } });
        const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barr });
        vkCmdPipelineBarrier2(cmd, &dep);
    };

    auto cmd = frame_datas[0].cmdpool->begin();
    for(auto i = 0ull; i < frame_datas.size(); ++i)
    {
        insert_vk_img_barrier(cmd, frame_datas[i].hiz_pyramid.get(), VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_GENERAL);
        insert_vk_img_barrier(cmd, frame_datas[i].hiz_debug_output.get(), VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        auto& img = frame_datas[i].gbuffer.depth_buffer_image.get();
        insert_vk_img_barrier(cmd, img, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearDepthStencilValue clear{ .depth = 0.0f, .stencil = 0 };
        VkImageSubresourceRange range{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        vkCmdClearDepthStencilImage(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        insert_vk_img_barrier(cmd, img, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    frame_datas[0].cmdpool->end(cmd);
    submit_queue->with_cmd_buf(cmd).submit_wait(-1ull);
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
    const auto frame_num = Engine::get().frame_num();
    submit_queue->wait_fence(fd.fen_rendering_finished, -1ull); // todo: maybe wait here for 10secs and crash to desktop.
    fd.cmdpool->reset();

    uint32_t swapchain_index{};
    Image* swapchain_image{};
    {
        VkResult acquire_ret;
        swapchain_index = swapchain.acquire(&acquire_ret, ~0ull, fd.sem_swapchain);
        if(acquire_ret != VK_SUCCESS)
        {
            ENG_WARN("Acquire image failed with: {}", static_cast<uint32_t>(acquire_ret));
            return;
        }
        swapchain_image = &swapchain.images[swapchain_index];
    }

    submit_queue->reset_fence(get_frame_data().fen_rendering_finished);
    // vkResetFences(dev, 1, &get_frame_data().fen_rendering_finished.fence);

    static glm::mat4 s_view = Engine::get().camera->prev_view;
    if((glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
    {
        s_view = Engine::get().camera->prev_view;
    }

    {
        const float hx = (halton(Engine::get().frame_num() % 4u, 2) * 2.0 - 1.0);
        const float hy = (halton(Engine::get().frame_num() % 4u, 3) * 2.0 - 1.0);
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

        staging_buffer->stage(fd.constants, constants, 0ull);
        // staging_buffer->stage(vsm.constants_buffer, vsmconsts, 0ull);
    }

    if(flags.test_clear(RenderFlags::DIRTY_TRANSFORMS_BIT))
    {
        assert(false);
        // upload_transforms();
    }

    bake_indirect_commands();

    staging_buffer->flush();

    static constexpr auto insert_vk_barrier = [](VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                                 VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        const auto barr = Vks(VkMemoryBarrier2{
            .srcStageMask = src_stage, .srcAccessMask = src_access, .dstStageMask = dst_stage, .dstAccessMask = dst_access });
        const auto dep = Vks(VkDependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &barr });
        vkCmdPipelineBarrier2(cmd, &dep);
    };
    static constexpr auto insert_vk_img_barrier = [](VkCommandBuffer cmd, Image& img, VkPipelineStageFlags2 src_stage,
                                                     VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                                                     VkAccessFlags2 dst_access, VkImageLayout old_layout, VkImageLayout new_layout) {
        const auto barr = Vks(VkImageMemoryBarrier2{ .srcStageMask = src_stage,
                                                     .srcAccessMask = src_access,
                                                     .dstStageMask = dst_stage,
                                                     .dstAccessMask = dst_access,
                                                     .oldLayout = old_layout,
                                                     .newLayout = new_layout,
                                                     .image = img.image,
                                                     .subresourceRange = { img.deduce_aspect(), 0, img.mips, 0, img.layers } });
        const auto dep = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barr });
        vkCmdPipelineBarrier2(cmd, &dep);
    };

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
                                                 .transforms_index = bindless_pool->get_index(fd.transform_buffers),
                                                 .indirect_commands_index = bindless_pool->get_index(geom_main_bufs.buf_draw_cmds) };

    {
        auto& dep_image = fd.gbuffer.depth_buffer_image.get();
        auto& hiz_image = fd.hiz_pyramid.get();

        if((glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
        {
            {
                VkClearDepthStencilValue clear{ .depth = 0.0f, .stencil = 0 };
                VkImageSubresourceRange range{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, hiz_image.mips, 0, 1 };
                vkCmdClearDepthStencilImage(cmd, hiz_image.image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
                insert_vk_barrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT);

                insert_vk_img_barrier(cmd, fd.gbuffer.depth_buffer_image.get(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                      VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                                      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

            push_constants_culling.hiz_width = hiz_image.extent.width;
            push_constants_culling.hiz_height = hiz_image.extent.height;

            auto* md = (PipelineMetadata*)hiz_pipeline->metadata;
            vkCmdBindPipeline(cmd, md->bind_point, md->pipeline);
            bindless_pool->bind(cmd, md->bind_point);
            for(auto i = 0u; i < hiz_image.mips; ++i)
            {
                if(i == 0)
                {
                    push_constants_culling.hiz_source =
                        bindless_pool->get_index(make_texture(fd.gbuffer.depth_buffer_image,
                                                              dep_image.create_image_view(ImageViewDescriptor{ .aspect = VK_IMAGE_ASPECT_DEPTH_BIT }),
                                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, hiz_sampler));
                }
                else
                {
                    push_constants_culling.hiz_source =
                        bindless_pool->get_index(make_texture(fd.hiz_pyramid,
                                                              hiz_image.create_image_view(ImageViewDescriptor{
                                                                  .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .mips = { i - 1, 1 } }),
                                                              VK_IMAGE_LAYOUT_GENERAL, hiz_sampler));
                }
                push_constants_culling.hiz_dest =
                    bindless_pool->get_index(make_texture(fd.hiz_pyramid,
                                                          hiz_image.create_image_view(ImageViewDescriptor{
                                                              .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .mips = { i, 1 } }),
                                                          VK_IMAGE_LAYOUT_GENERAL, (VkSampler)(~0ull)));
                push_constants_culling.hiz_width = std::max(hiz_image.extent.width >> i, 1u);
                push_constants_culling.hiz_height = std::max(hiz_image.extent.height >> i, 1u);
                vkCmdPushConstants(cmd, bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                                   sizeof(push_constants_culling), &push_constants_culling);
                vkCmdDispatch(cmd, (push_constants_culling.hiz_width + 31) / 32, (push_constants_culling.hiz_height + 31) / 32, 1);
                insert_vk_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
                                  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR);
            }
        }
        else
        {
            auto* md = (PipelineMetadata*)hiz_pipeline->metadata;
            bindless_pool->bind(cmd, md->bind_point);
            insert_vk_img_barrier(cmd, fd.gbuffer.depth_buffer_image.get(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                  VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                                  VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        push_constants_culling.hiz_source =
            bindless_pool->get_index(make_texture(fd.hiz_pyramid,
                                                  hiz_image.create_image_view(ImageViewDescriptor{
                                                      .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, .mips = { 0u, hiz_image.mips } }),
                                                  VK_IMAGE_LAYOUT_GENERAL, hiz_sampler));
        push_constants_culling.hiz_dest =
            bindless_pool->get_index(make_texture(fd.hiz_debug_output,
                                                  fd.hiz_debug_output->create_image_view(ImageViewDescriptor{ .aspect = VK_IMAGE_ASPECT_COLOR_BIT }),
                                                  VK_IMAGE_LAYOUT_GENERAL, (VkSampler)(~0ull)));
        {
            VkClearColorValue clear{ .float32 = 0.0f };
            VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(cmd, fd.hiz_debug_output->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
            insert_vk_barrier(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT);
        }
        vkCmdPushConstants(cmd, bindless_pool->get_pipeline_layout(), VK_SHADER_STAGE_ALL, 0,
                           sizeof(push_constants_culling), &push_constants_culling);
        auto* md = (PipelineMetadata*)cull_pipeline->metadata;
        vkCmdBindPipeline(cmd, md->bind_point, md->pipeline);
        vkCmdDispatch(cmd, (meshlet_instances.size() + 63) / 64, 1, 1);
        insert_vk_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    }

    VkRenderingAttachmentInfo rainfos[]{
        Vks(VkRenderingAttachmentInfo{ .imageView = swapchain_image->get_image_view(),
                                       .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                       .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                       .clearValue = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } } }),
        Vks(VkRenderingAttachmentInfo{ .imageView = fd.gbuffer.depth_buffer_image->get_image_view(),
                                       .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                       .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                       .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0 } } })
    };
    const auto rinfo =
        Vks(VkRenderingInfo{ .renderArea = { { 0, 0 }, { swapchain_image->extent.width, swapchain_image->extent.height } },
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
        .transforms_index = ~0u,
        .constants_index = bindless_pool->get_index(fd.constants),
        .meshlet_instance_index = bindless_pool->get_index(geom_main_bufs.buf_draw_ids),
        .meshlet_ids_index = bindless_pool->get_index(geom_main_bufs.buf_final_draw_ids),
        .meshlet_bs_index = bindless_pool->get_index(geom_main_bufs.buf_draw_bs),
        .hiz_pyramid_index = push_constants_culling.hiz_source,
        .hiz_debug_index = bindless_pool->get_index(make_texture(
            fd.hiz_debug_output, fd.hiz_debug_output->create_image_view(ImageViewDescriptor{ .aspect = VK_IMAGE_ASPECT_COLOR_BIT }),
            VK_IMAGE_LAYOUT_GENERAL, (VkSampler)*batch_sampler(SamplerDescriptor{ .mip_lod = { 0.0f, 1.0f, 0.0 } }))),
    };

    vkCmdBindIndexBuffer(cmd, geom_main_bufs.buf_indices->buffer, 0, VK_INDEX_TYPE_UINT16);
    insert_vk_img_barrier(cmd, *swapchain_image, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
    insert_vk_img_barrier(cmd, fd.gbuffer.depth_buffer_image.get(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                          VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
    vkCmdBeginRendering(cmd, &rinfo);
    bindless_pool->bind(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
    VkViewport viewport{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
    VkRect2D scissor{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
    for(auto i = 0u, off = 0u; i < multibatches.size(); ++i)
    {
        const auto& mb = multibatches.at(i);
        const auto& p = pipelines.at(mb.pipeline);
        const auto* pm = static_cast<PipelineMetadata*>(p.metadata);
        vkCmdBindPipeline(cmd, pm->bind_point, pm->pipeline);
        vkCmdPushConstants(cmd, pm->layout, VK_SHADER_STAGE_ALL, 0u, sizeof(pc1), &pc1);
        vkCmdSetViewportWithCount(cmd, 1, &viewport);
        vkCmdSetScissorWithCount(cmd, 1, &scissor);
        vkCmdDrawIndexedIndirectCount(cmd, geom_main_bufs.buf_draw_cmds->buffer, 8, geom_main_bufs.buf_draw_cmds->buffer,
                                      0, geom_main_bufs.command_count, sizeof(DrawIndirectCommand));
    }
    vkCmdEndRendering(cmd);
    insert_vk_img_barrier(cmd, *swapchain_image, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE,
                          VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

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

void RendererVulkan::on_window_resize()
{
    flags.set(RenderFlags::RESIZE_SWAPCHAIN_BIT);
    // set_screen(ScreenRect{ .w = Engine::get().window->width, .h = Engine::get().window->height });
}

// void RendererVulkan::set_screen(ScreenRect screen) {
//     assert(false);
//     // screen_rect = screen;
//     //  ENG_WARN("TODO: Resize resources on new set_screen()");
// }

Handle<Image> RendererVulkan::batch_image(const ImageDescriptor& desc)
{
    const auto handle = make_image(ImageCreateInfo{ .name = desc.name,
                                                    .type = eng::to_vk(desc.type),
                                                    .extent = { desc.width, desc.height, 1 },
                                                    .format = eng::to_vk(desc.format),
                                                    .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT });
    staging_buffer->stage(handle, desc.data, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    return handle;
}

Handle<Sampler> RendererVulkan::batch_sampler(const SamplerDescriptor& batch)
{
    auto it = samplers.emplace(batch, nullptr);
    if(it.second)
    {
        auto info = Vks(VkSamplerCreateInfo{ .magFilter = eng::to_vk(batch.filtering[1]),
                                             .minFilter = eng::to_vk(batch.filtering[0]),
                                             .mipmapMode = eng::to_vk(batch.mipmap_mode),
                                             .addressModeU = eng::to_vk(batch.addressing[0]),
                                             .addressModeV = eng::to_vk(batch.addressing[1]),
                                             .addressModeW = eng::to_vk(batch.addressing[2]),
                                             .mipLodBias = batch.mip_lod[2],
                                             .minLod = batch.mip_lod[0],
                                             .maxLod = batch.mip_lod[1] });

        auto reduction = Vks(VkSamplerReductionModeCreateInfo{});
        if(batch.reduction_mode)
        {
            reduction.reductionMode = eng::to_vk(*batch.reduction_mode);
            info.pNext = &reduction;
        }
        VK_CHECK(vkCreateSampler(dev, &info, {}, &it.first->second));
    }
    return Handle<Sampler>{ reinterpret_cast<uint64_t>(it.first->second) };
}

Handle<Texture> RendererVulkan::batch_texture(const TextureDescriptor& batch)
{
    const auto view = batch.image->get_image_view();
    const auto layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    const auto sampler = reinterpret_cast<VkSampler>(*batch.sampler);
    assert(view);
    return make_texture(batch.image, view, layout, sampler);
}

Handle<Material> RendererVulkan::batch_material(const MaterialDescriptor& desc)
{
    return materials.insert(Material{ .mesh_pass = desc.mesh_pass, .base_color_texture = desc.base_color_texture }).first;
}

Handle<Geometry> RendererVulkan::batch_geometry(const GeometryDescriptor& batch)
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

    staging_buffer->stage(geom_main_bufs.buf_vpos, positions, STAGING_APPEND);
    staging_buffer->stage(geom_main_bufs.buf_vattrs, attributes, STAGING_APPEND);
    staging_buffer->stage(geom_main_bufs.buf_indices, out_indices, STAGING_APPEND);

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

    return handle.first;
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

Handle<Mesh> RendererVulkan::batch_mesh(const MeshDescriptor& batch)
{
    auto& bm = meshes.emplace_back(Mesh{ .geometry = batch.geometry, .material = batch.material });
    if(!bm.material) { bm.material = default_material; }
    return Handle<Mesh>{ meshes.size() - 1 };
}

Handle<Mesh> RendererVulkan::instance_mesh(const InstanceSettings& settings)
{
    if(!settings.mesh) { return {}; }
    const auto& mesh = settings.mesh.get();
    const auto& geom = mesh.geometry.get();
    meshlets_to_instance.reserve(meshlets_to_instance.size() + geom.meshlet_range.size);
    for(auto i = 0u; i < geom.meshlet_range.size; ++i)
    {
        meshlets_to_instance.push_back(MeshletInstance{ .mesh = (uint32_t)*settings.mesh,
                                                        .global_meshlet = (uint32_t)geom.meshlet_range.offset + i,
                                                        .meshlet = i,
                                                        .index = mesh_instance_index });
    }
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
    for(auto& e : shaders_to_compile)
    {
        auto& sh = e.get();
        sh.metadata = new ShaderMetadata{};
        ShaderMetadata* shmd = (ShaderMetadata*)sh.metadata;
        const auto& path = sh.path;
        static const auto read_file = [](const std::filesystem::path& path) {
            std::string path_str = path.string();
            std::string path_to_includes = (std::filesystem::path{ ENGINE_BASE_ASSET_PATH } / "shaders").string();
            char error[256] = {};
            char* parsed_file = stb_include_file(path_str.data(), nullptr, path_to_includes.data(), error);
            if(!parsed_file)
            {
                ENG_WARN("STBI_INCLUDE cannot parse file [{}]: {}", path_str, error);
                return std::string{};
            }
            std::string parsed_file_str{ parsed_file };
            free(parsed_file);
            return parsed_file_str;
        };

        const auto kind = [stage = sh.stage] {
            if(stage == Shader::Stage::VERTEX) { return shaderc_vertex_shader; }
            if(stage == Shader::Stage::PIXEL) { return shaderc_fragment_shader; }
            // if(stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) { return shaderc_raygen_shader; }
            // if(stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) { return shaderc_closesthit_shader; }
            // if(stage == VK_SHADER_STAGE_MISS_BIT_KHR) { return shaderc_miss_shader; }
            if(stage == Shader::Stage::COMPUTE) { return shaderc_compute_shader; }
            ENG_ERROR("Unrecognized shader type");
            return shaderc_vertex_shader;
        }();

        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
        options.SetTargetSpirv(shaderc_spirv_version_1_6);
        options.SetGenerateDebugInfo();

        // options.AddMacroDefinition("ASDF");
        shaderc::Compiler c;
        std::string file_str = read_file(path);
        const auto res = c.CompileGlslToSpv(file_str, kind, path.filename().string().c_str(), options);
        if(res.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            ENG_WARN("Could not compile shader : {}, because : \"{}\"", path.string(), res.GetErrorMessage());
            return;
        }

        const auto module_info = Vks(VkShaderModuleCreateInfo{
            .codeSize = (res.end() - res.begin()) * sizeof(uint32_t),
            .pCode = res.begin(),
        });
        VK_CHECK(vkCreateShaderModule(dev, &module_info, nullptr, &shmd->shader));
    }

    shaders_to_compile.clear();
}

void RendererVulkan::compile_pipelines()
{
    for(auto& e : pipelines_to_compile)
    {
        auto& p = e.get();
        const auto& info = p.info;
        p.metadata = new PipelineMetadata{};
        auto* pm = (PipelineMetadata*)p.metadata;

        {
            const auto stage = info.shaders[0]->stage;
            if(stage == Shader::Stage::VERTEX) { pm->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS; }
            else if(stage == Shader::Stage::COMPUTE) { pm->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE; }
            else
            {
                assert(false);
                continue;
            }
        }

        pm->layout = bindless_pool->get_pipeline_layout();

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(info.shaders.size());
        for(const auto& e : info.shaders)
        {
            stages.push_back(Vks(VkPipelineShaderStageCreateInfo{
                .stage = eng::to_vk(e->stage), .module = ((ShaderMetadata*)e->metadata)->shader, .pName = "main" }));
        }

        if(pm->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
        {
            const auto vkinfo = Vks(VkComputePipelineCreateInfo{ .stage = stages.at(0), .layout = pm->layout });
            VK_CHECK(vkCreateComputePipelines(dev, {}, 1, &vkinfo, {}, &pm->pipeline));
            continue;
        }

        auto pVertexInputState = Vks(VkPipelineVertexInputStateCreateInfo{});

        auto pInputAssemblyState = Vks(VkPipelineInputAssemblyStateCreateInfo{ .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST });

        auto pTessellationState = Vks(VkPipelineTessellationStateCreateInfo{});

        auto pViewportState = Vks(VkPipelineViewportStateCreateInfo{});

        auto pRasterizationState = Vks(VkPipelineRasterizationStateCreateInfo{
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = eng::to_vk(info.culling),
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        });

        auto pMultisampleState = Vks(VkPipelineMultisampleStateCreateInfo{
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        });

        auto pDepthStencilState = Vks(VkPipelineDepthStencilStateCreateInfo{
            .depthTestEnable = info.depth_test,
            .depthWriteEnable = info.depth_write,
            .depthCompareOp = eng::to_vk(info.depth_compare),
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
            .front = {},
            .back = {},
        });

        std::array<VkPipelineColorBlendAttachmentState, 4> blends;
        for(uint32_t i = 0; i < 1; ++i)
        {
            blends[i] = { .colorWriteMask = 0b1111 /*RGBA*/ };
        }
        auto pColorBlendState = Vks(VkPipelineColorBlendStateCreateInfo{
            .attachmentCount = 1,
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

        std::vector<VkFormat> col_formats(info.color_formats.size());
        for(auto i = 0u; i < info.color_formats.size(); ++i)
        {
            col_formats.at(i) = eng::to_vk(info.color_formats.at(i));
        }
        auto pDynamicRendering = Vks(VkPipelineRenderingCreateInfo{
            .colorAttachmentCount = (uint32_t)col_formats.size(),
            .pColorAttachmentFormats = col_formats.data(),
            .depthAttachmentFormat = eng::to_vk(info.depth_format),
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
        VK_CHECK(vkCreateGraphicsPipelines(dev, nullptr, 1, &vk_info, nullptr, &pm->pipeline));
    }
}

void RendererVulkan::bake_indirect_commands()
{
    if(!meshlets_to_instance.empty())
    {
        meshlet_instances.reserve(meshlet_instances.size() + meshlets_to_instance.size());
        meshlet_instances.insert(meshlet_instances.end(), meshlets_to_instance.begin(), meshlets_to_instance.end());
        meshlets_to_instance.clear();

        std::sort(meshlet_instances.begin(), meshlet_instances.end(), [this](const auto& a, const auto& b) {
            const auto& ma = meshes.at(a.mesh);
            const auto& mb = meshes.at(b.mesh);
            if(ma.material >= mb.material) { return false; }           // first sort by material
            if(a.global_meshlet >= b.global_meshlet) { return false; } // then sort by geometry
            return true;
        });
    }

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
        const auto& m = meshes.at(mi.mesh);
        const auto& mp = mesh_passes.at(m.material->mesh_pass);
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
            const auto& g = m.geometry.get();
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

    std::vector<uint32_t> final_ids(meshlet_instances.size());

    staging_buffer->stage(geom_main_bufs.buf_draw_cmds, (uint32_t)gpu_cmds.size(), 0);
    staging_buffer->stage(geom_main_bufs.buf_draw_cmds, 0, 4);
    staging_buffer->stage(geom_main_bufs.buf_draw_cmds, gpu_cmds, 8);
    staging_buffer->stage(geom_main_bufs.buf_draw_ids, (uint32_t)meshlet_instances.size(), 0);
    staging_buffer->stage(geom_main_bufs.buf_draw_ids, gpu_ids, 8);
    staging_buffer->stage(geom_main_bufs.buf_final_draw_ids, final_ids, 0);
    staging_buffer->stage(geom_main_bufs.buf_draw_bs, gpu_bbs, 0);
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
    Fence f{ dev, false };
    gq.submit(cmd, &f);
    f.wait();
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

Handle<Shader> RendererVulkan::make_shader(Shader::Stage stage, const std::filesystem::path& path)
{
    auto ret = shaders.emplace(path, Shader{ .path = paths::canonize_path(path, "shaders"), .stage = stage });
    const auto handle = Handle<Shader>{ (uintptr_t)&ret.first->second };
    if(ret.second) { shaders_to_compile.push_back(handle); }
    return handle;
}

Handle<Pipeline> RendererVulkan::make_pipeline(const PipelineCreateInfo& info)
{
    Pipeline p{ .info = info };
    auto ret = pipelines.insert(std::move(p));
    if(ret.second) { pipelines_to_compile.push_back(ret.first); }
    return ret.first;
}

Handle<ShaderEffect> RendererVulkan::make_shader_effect(const ShaderEffect& info)
{
    return shader_effects.insert(info).first;
}

Handle<MeshPass> RendererVulkan::make_mesh_pass(const MeshPassCreateInfo& info)
{
    auto it = mesh_passes.emplace(info.name, MeshPass{ .effects = info.effects });
    return Handle<MeshPass>{ (uintptr_t)&it.first->second };
}

Handle<Buffer> RendererVulkan::make_buffer(const BufferCreateInfo& info)
{
    auto handle = buffers.emplace(info);
    handle->init();
    return handle;
}

Handle<Image> RendererVulkan::make_image(const ImageCreateInfo& info)
{
    auto handle = images.emplace(info);
    handle->init();
    return handle;
}

Handle<Texture> RendererVulkan::make_texture(Handle<Image> image, VkImageView view, VkImageLayout layout, VkSampler sampler)
{
    return textures
        .insert(Texture{ .image = image,
                         .view = Handle<ImageView>{ reinterpret_cast<uintptr_t>(view) },
                         .layout = Handle<ImageLayout>{ static_cast<uint64_t>(std::to_underlying(layout)) },
                         .sampler = Handle<Sampler>{ reinterpret_cast<uintptr_t>(sampler) } })
        .first;
}

Handle<Texture> gfx::RendererVulkan::make_texture(Handle<Image> image, VkImageLayout layout, VkSampler sampler)
{
    const auto view = image->create_image_view();
    return make_texture(image, view, layout, sampler);
}

void RendererVulkan::destroy_buffer(Handle<Buffer> buffer)
{
    buffer->destroy();
    buffers.erase(buffer);
}

void gfx::RendererVulkan::destroy_image(Handle<Image> image)
{
    image->destroy();
    images.erase(image);
}

uint32_t RendererVulkan::get_bindless(Handle<Buffer> buffer) { return bindless_pool->get_index(buffer); }

void RendererVulkan::update_resource(Handle<Buffer> dst) { bindless_pool->update_index(dst); }

FrameData& RendererVulkan::get_frame_data(uint32_t offset)
{
    return frame_datas[(Engine::get().frame_num() + offset) % frame_datas.size()];
}

const FrameData& RendererVulkan::get_frame_data(uint32_t offset) const
{
    return const_cast<RendererVulkan*>(this)->get_frame_data();
}

void Swapchain::create(VkDevice dev, uint32_t image_count, uint32_t width, uint32_t height)
{
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

    for(uint32_t i = 0; i < image_count; ++i)
    {
        images[i] = Image{};
        images[i].name = fmt::format("swapchain_image_{}", i);
        images[i].type = VK_IMAGE_TYPE_2D;
        images[i].image = vk_images.at(i);
        images[i].extent = VkExtent3D{ sinfo.imageExtent.width, sinfo.imageExtent.height, 0u };
        images[i].format = sinfo.imageFormat;
        images[i].mips = 1;
        images[i].layers = 1;
        images[i].usage = sinfo.imageUsage;
        views[i] = images[i].create_image_view();
    }
}

uint32_t Swapchain::acquire(VkResult* res, uint64_t timeout, VkSemaphore semaphore, VkFence fence)
{
    uint32_t idx;
    auto result = vkAcquireNextImageKHR(RendererVulkan::get_instance()->dev, swapchain, timeout, semaphore, fence, &idx);
    if(res) { *res = result; }
    current_index = idx;
    return idx;
}

Image& Swapchain::get_current_image() { return images.at(current_index); }

VkImageView& Swapchain::get_current_view() { return views.at(current_index); }

} // namespace gfx