#include <numeric>
#include <fstream>
#include <utility>
#include <volk/volk.h>
#include <glm/mat3x3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

RendererVulkan& get_renderer() { return *static_cast<RendererVulkan*>(Engine::renderer()); }

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

template <typename Storage> struct HandleDispatcher<RenderModel, Storage> {
    constexpr RenderModel* operator()(const Handle<RenderModel, Storage>& h) const {
        return &get_renderer().models.at(h);
    }
};
template <typename Storage> struct HandleDispatcher<BatchedModel, Storage> {
    constexpr RenderModel* operator()(const Handle<BatchedModel, Storage>& h) const {
        return &get_renderer().models.at(Handle<RenderModel, Storage>{ *h });
    }
};
template <typename Storage> struct HandleDispatcher<RenderModelRTMetadata, Storage> {
    constexpr RenderModelRTMetadata* operator()(const Handle<RenderModelRTMetadata, Storage>& h) const {
        return &get_renderer().rt_metadata.at(h);
    }
};
template <typename Storage> struct HandleDispatcher<RenderModelInstance, Storage> {
    constexpr RenderModelInstance* operator()(const Handle<RenderModelInstance, Storage>& h) const {
        return &get_renderer().model_instances.at(h);
    }
};
template <typename Storage> struct HandleDispatcher<InstancedModel, Storage> {
    constexpr RenderModelInstance* operator()(const Handle<InstancedModel, Storage>& h) const {
        return &get_renderer().model_instances.at(Handle<RenderModelInstance, Storage>{ *h });
    }
};
template <typename Storage> struct HandleDispatcher<RenderMesh, Storage> {
    constexpr RenderMesh* operator()(const Handle<RenderMesh, Storage>& h) const {
        return &get_renderer().meshes.at(*h);
    }
};

Buffer::Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map)
    : Buffer(name, size, 1u, usage, map) {}

Buffer::Buffer(const std::string& name, size_t size, uint32_t alignment, VkBufferUsageFlags usage, bool map)
    : Buffer(name, VkBufferCreateInfo{ .size = size, .usage = usage },
             VmaAllocationCreateInfo{
                 .flags = (map ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u),
                 .usage = VMA_MEMORY_USAGE_AUTO,
             },
             alignment) {}

Buffer::Buffer(const std::string& name, vks::BufferCreateInfo create_info, VmaAllocationCreateInfo alloc_info, uint32_t alignment)
    : name{ name }, capacity{ create_info.size }, alignment{ alignment } {
    uint32_t queue_family_indices[]{ get_renderer().gqi, get_renderer().tqi1 };
    if(queue_family_indices[0] != queue_family_indices[1]) {
        create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    }
    if(!(alloc_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        create_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
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
    ENG_LOG("ALLOCATING BUFFER {} OF SIZE {:.2f} KB", name.c_str(), static_cast<float>(size) / 1024.0f);
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

Buffer::PushResult Buffer::push_data(std::span<const std::byte> data, uint32_t offset) {
    if(offset > capacity) {
        ENG_WARN("Provided buffer offset is bigger than the capacity");
        return std::make_pair(false, std::make_shared<std::latch>(0u));
    }

    const auto size_after = offset + data.size_bytes();

    if(size_after > capacity) {
        size_t new_size = std::ceill(static_cast<long double>(capacity) * 1.5l);
        if(new_size < size_after) { new_size = size_after; }
        ENG_LOG("Resizing buffer {}", name);
        if(!resize(new_size)) {
            ENG_LOG("Failed to resize the buffer {}", name);
            return std::make_pair(false, std::make_shared<std::latch>(0u));
        }
    }

    std::shared_ptr<std::latch> return_latch = std::make_shared<std::latch>(0u);

    if(mapped) {
        memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size_bytes());
    } else {
        return_latch = get_renderer().staging->send_to(buffer, offset, data.data(), data.size_bytes());
    }

    return_latch->wait();

    size = std::max(size, offset + data.size_bytes());

    return std::make_pair(true, return_latch);
}

bool Buffer::resize(size_t new_size) {
    Buffer new_buffer{ name, new_size, alignment, usage, !!mapped };
    const auto ret = new_buffer.push_data(std::span{ static_cast<const std::byte*>(mapped), size });
    if(!ret.first) { return false; }
    ret.second->wait();
    *this = std::move(new_buffer);
    return true;
}

void RendererVulkan::init() {
    initialize_vulkan();
    create_swapchain();
    create_rt_output_image();
    compile_shaders();
    build_pipelines();
    build_sbt();
    initialize_imgui();
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

    vks::CommandPoolCreateInfo cmdpi;
    cmdpi.queueFamilyIndex = gqi;
    VK_CHECK(vkCreateCommandPool(device, &cmdpi, nullptr, &cmdpool));

    global_buffer =
        Buffer{ "globals", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
    global_buffer.push_data(&(const glm::mat4&)Engine::camera()->get_projection(), sizeof(glm::mat4), 1 * sizeof(glm::mat4));
    global_buffer.push_data(&(const glm::mat4&)glm::inverse(Engine::camera()->get_projection()), sizeof(glm::mat4),
                            3 * sizeof(glm::mat4));

    vertex_buffer = Buffer{ "vertex_buffer", 0ull,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            false };
    index_buffer = Buffer{ "index_buffer", 0ull,
                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                           false };

    vks::SemaphoreCreateInfo sem_swapchain_info;
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_swapchain_image));
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_rendering_finished));
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_gui_start));
    VK_CHECK(vkCreateSemaphore(dev, &sem_swapchain_info, nullptr, &primitives.sem_copy_to_sw_img_done));
}

void RendererVulkan::create_swapchain() {
    vks::SwapchainCreateInfoKHR sinfo;
    sinfo.surface = window_surface;
    sinfo.minImageCount = 2;
    sinfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
    sinfo.imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
    sinfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sinfo.imageExtent = VkExtent2D{ Engine::window()->size[0], Engine::window()->size[1] };
    sinfo.imageArrayLayers = 1;
    sinfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sinfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sinfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sinfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sinfo.clipped = true;

    VkFormat view_formats[]{
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
    };

    VkImageFormatListCreateInfo format_list_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2,
        .pViewFormats = view_formats,
    };
    sinfo.pNext = &format_list_info;

    VK_CHECK(vkCreateSwapchainKHR(dev, &sinfo, nullptr, &swapchain));
    uint32_t num_images;
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, nullptr);

    std::vector<VkImage> images(num_images);
    vkGetSwapchainImagesKHR(dev, swapchain, &num_images, images.data());

    for(uint32_t counter = 0; VkImage img : images) {
        swapchain_images.push_back(Image{ std::format("swapchain_image_{}", counter), img, sinfo.imageExtent.width,
                                          sinfo.imageExtent.height, 1u, 1u, sinfo.imageArrayLayers, sinfo.imageFormat,
                                          VK_SAMPLE_COUNT_1_BIT, sinfo.imageUsage });
    }
    swapchain_format = sinfo.imageFormat;
}

void RendererVulkan::initialize_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(Engine::window()->window, true);

    VkFormat color_formats[]{ VK_FORMAT_R8G8B8A8_UNORM };

    ImGui_ImplVulkan_InitInfo init_info = { 
        .Instance = instance,
        .PhysicalDevice = pdev,
        .Device = dev,
        .QueueFamily = gqi,
        .Queue = gq,
        .DescriptorPool = imgui_desc_pool,
        .MinImageCount = (uint32_t)swapchain_images.size(),
        .ImageCount = (uint32_t)swapchain_images.size(),
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

    auto cmdimgui = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    ImGui_ImplVulkan_CreateFontsTexture();
    end_recording(cmdimgui);
    scheduler_gq.enqueue_wait_submit({ { cmdimgui } });
    vkQueueWaitIdle(gq);
}

// aligns a to the nearest multiple any power of two
template <std::integral T> static T align_up(T a, T b) { return (a + b - 1) & -b; }

static std::vector<HandleInstancedModel> probe_visualizers;
static std::vector<glm::mat4x3> original_transforms;
static HandleBatchedModel sphere;

void RendererVulkan::render() {
    reset_command_pool(cmdpool);
    if(flags & RendererFlags::DIRTY_MODEL_BATCHES) {
        upload_staged_models();
        flags ^= RendererFlags::DIRTY_MODEL_BATCHES;
    }
    if(flags & RendererFlags::DIRTY_MODEL_INSTANCES) {
        upload_instances();
        flags ^= RendererFlags::DIRTY_MODEL_INSTANCES;
    }
    if(flags & RendererFlags::DIRTY_BLAS) {
        build_blas();
        flags ^= RendererFlags::DIRTY_BLAS;
    }
    if(flags & RendererFlags::DIRTY_TLAS) {
        build_tlas();
        prepare_ddgi();
        flags ^= RendererFlags::DIRTY_TLAS;
    }
    if(flags & RendererFlags::REFIT_TLAS) {
        refit_tlas();
        flags ^= RendererFlags::REFIT_TLAS;
    }
    if(flags & RendererFlags::UPDATE_MESH_POSITIONS) {
        upload_transforms();
        flags ^= RendererFlags::UPDATE_MESH_POSITIONS;
    }

    VkDescriptorSet frame_desc_set =
        descriptor_pool_allocator.allocate_set(per_frame_desc_pool, layouts.at(0).descriptor_layouts.at(0), textures.size());

    VkSampler linear_sampler = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    DescriptorSetWriter per_frame_set_writer;
    per_frame_set_writer.write(0, 0, tlas)
        .write(1, 0, rt_image.view, {}, VK_IMAGE_LAYOUT_GENERAL)
        .write(2, 0, ddgi.radiance_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
        .write(3, 0, ddgi.irradiance_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
        .write(4, 0, ddgi.visibility_texture.view, linear_sampler, VK_IMAGE_LAYOUT_GENERAL)
        .write(5, 0, ddgi.probe_offsets_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL)
        .write(6, 0, ddgi.irradiance_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL)
        .write(7, 0, ddgi.visibility_texture.view, {}, VK_IMAGE_LAYOUT_GENERAL);
    for(uint32_t i = 0; i < textures.size(); ++i) {
        per_frame_set_writer.write(15, i, textures.at(i).view, linear_sampler, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }
    per_frame_set_writer.update(frame_desc_set, layouts.at(0), 0);

    const float hx = (halton(Engine::frame_num() % 4u, 2) * 2.0 - 1.0);
    const float hy = (halton(Engine::frame_num() % 4u, 3) * 2.0 - 1.0);
    const glm::mat3 rand_mat =
        glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));

    global_buffer.push_data(&(const glm::mat4&)Engine::camera()->get_view(), sizeof(glm::mat4), 0);
    global_buffer.push_data(&(const glm::mat4&)glm::inverse(Engine::camera()->get_view()), sizeof(glm::mat4),
                            2 * sizeof(glm::mat4));
    global_buffer.push_data(&rand_mat, sizeof(glm::mat3), 4 * sizeof(glm::mat4));

    uint32_t sw_img_idx;
    vkAcquireNextImageKHR(dev, swapchain, -1ULL, primitives.sem_swapchain_image, nullptr, &sw_img_idx);
    ImageStatefulBarrier sw_img_barrier{ swapchain_images.at(sw_img_idx) };

    {
        const auto frame_num = Engine::frame_num();
        ddgi_buffer.push_data(&frame_num, sizeof(uint32_t), offsetof(DDGI_Buffer, frame_num));

        auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VkMemoryBarrier mem_barrier{ .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                     .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                     .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, {}, 1,
                             &mem_barrier, 0, nullptr, 0, nullptr);

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
        hit_sbt.size = handle_size_aligned * 2;
        hit_sbt.stride = handle_size_aligned;

        vks::StridedDeviceAddressRegionKHR callable_sbt;

        const auto* window = Engine::window();
        uint32_t mode = 0;
        // clang-format off
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 0 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &global_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 1 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_datas_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 2 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &vertex_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &index_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 4 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 5 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &tlas_mesh_offsets_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 6 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &blas_mesh_offsets_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL, 7 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &triangle_mesh_ids_buffer.bda);
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
                          pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, 0, 1, &frame_desc_set, 0, nullptr);

        // radiance pass
        mode = 1;
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           8 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdTraceRaysKHR(cmd, &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, ddgi.rays_per_probe,
                          ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z, 1);

        radiance_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.at(RendererPipelineType::DDGI_PROBE_UPDATE).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, 0, 1, &frame_desc_set, 0, nullptr);

        // irradiance pass, only need radiance texture
        mode = 0;
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           8 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.irradiance_texture.width / 8u),
                      std::ceilf((float)ddgi.irradiance_texture.height / 8u), 1u);

        // visibility pass, only need radiance texture
        mode = 1;
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST).layout, VK_SHADER_STAGE_ALL,
                           8 * sizeof(VkDeviceAddress), sizeof(mode), &mode);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.visibility_texture.width / 8u),
                      std::ceilf((float)ddgi.visibility_texture.height / 8u), 1u);

        // probe offset pass, only need radiance texture to complete
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.at(RendererPipelineType::DDGI_PROBE_OFFSET).pipeline);
        vkCmdDispatch(cmd, std::ceilf((float)ddgi.probe_offsets_texture.width / 8.0f),
                      std::ceilf((float)ddgi.probe_offsets_texture.height / 8.0f), 1u);

        irradiance_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        visibility_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
        offset_image_barrier.insert_barrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

        VkRenderingAttachmentInfo r_col_att_1{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain_images.at(sw_img_idx).view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
        };
        VkRenderingAttachmentInfo r_col_atts[]{ r_col_att_1 };

        VkRenderingAttachmentInfo r_dep_att{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth_buffers[sw_img_idx].view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .depthStencil = { 1.0f, 0 } },
        };

        VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = VkRect2D{ .offset = { 0, 0 }, .extent = { Engine::window()->size[0], Engine::window()->size[1] } },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = r_col_atts,
            .pDepthAttachment = &r_dep_att,
        };

        ImageStatefulBarrier depth_buffer_barrier{ depth_buffers[sw_img_idx] };

        VkDeviceSize vb_offsets[]{ 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer.buffer, vb_offsets);
        vkCmdBindIndexBuffer(cmd, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.at(RendererPipelineType::DEFAULT_UNLIT).pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelines.at(RendererPipelineType::DEFAULT_UNLIT).layout, 0, 1, &frame_desc_set, 0, nullptr);

        sw_img_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        depth_buffer_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        vkCmdBeginRendering(cmd, &rendering_info);
        VkRect2D r_sciss_1{ .offset = {}, .extent = { Engine::window()->size[0], Engine::window()->size[1] } };
        VkViewport r_view_1{ .x = 0.0f,
                             .y = static_cast<float>(Engine::window()->size[1]),
                             .width = static_cast<float>(Engine::window()->size[0]),
                             .height = -static_cast<float>(Engine::window()->size[1]),
                             .minDepth = 0.0f,
                             .maxDepth = 1.0f };
        vkCmdSetScissorWithCount(cmd, 1, &r_sciss_1);
        vkCmdSetViewportWithCount(cmd, 1, &r_view_1);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           0 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &global_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           1 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_datas_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           2 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instance_mesh_id_buffer.bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           3 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &mesh_instance_transform_buffer[0]->bda);
        vkCmdPushConstants(cmd, pipelines.at(RendererPipelineType::DEFAULT_UNLIT).layout, VK_SHADER_STAGE_ALL,
                           4 * sizeof(VkDeviceAddress), sizeof(VkDeviceAddress), &ddgi_buffer.bda);
        vkCmdDrawIndexedIndirectCount(cmd, indirect_draw_buffer.buffer, sizeof(IndirectDrawCommandBufferHeader),
                                      indirect_draw_buffer.buffer, 0ull, mesh_instances.size(),
                                      sizeof(VkDrawIndexedIndirectCommand));
        vkCmdEndRendering(cmd);

        vks::ImageViewCreateInfo i_sw_img_view;
        i_sw_img_view.format = VK_FORMAT_R8G8B8A8_UNORM;
        i_sw_img_view.image = swapchain_images.at(sw_img_idx).image;
        i_sw_img_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        i_sw_img_view.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        };
        VkImageView i_sw_view{};
        VK_CHECK(vkCreateImageView(dev, &i_sw_img_view, nullptr, &i_sw_view));

        VkRenderingAttachmentInfo i_col_atts[]{
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = i_sw_view,
                .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } },
            },
        };
        VkRenderingInfo imgui_rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = VkRect2D{ .offset = { 0, 0 }, .extent = { Engine::window()->size[0], Engine::window()->size[1] } },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = i_col_atts,
        };

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("a");
        ImGui::Text("asdf");
        ImGui::End();
        ImGui::Render();
        ImDrawData* im_draw_data = ImGui::GetDrawData();

        vkCmdBeginRendering(cmd, &imgui_rendering_info);
        vkCmdSetScissor(cmd, 0, 1, &r_sciss_1);
        vkCmdSetViewport(cmd, 0, 1, &r_view_1);
        ImGui_ImplVulkan_RenderDrawData(im_draw_data, cmd);
        vkCmdEndRendering(cmd);

        sw_img_barrier.insert_barrier(cmd, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_NONE);

        vkEndCommandBuffer(cmd);

        scheduler_gq.enqueue_wait_submit(RecordingSubmitInfo{
            .buffers = { cmd },
            .waits = { { primitives.sem_swapchain_image, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT } },
            .signals = { { primitives.sem_rendering_finished, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT } },
        });

        vks::PresentInfoKHR pinfo;
        pinfo.swapchainCount = 1;
        pinfo.pSwapchains = &swapchain;
        pinfo.pImageIndices = &sw_img_idx;
        pinfo.waitSemaphoreCount = 1;
        pinfo.pWaitSemaphores = &primitives.sem_rendering_finished;
        vkQueuePresentKHR(gq, &pinfo);
        vkQueueWaitIdle(gq);
        vkDestroyImageView(dev, i_sw_view, nullptr);

        descriptor_pool_allocator.reset_pool(per_frame_desc_pool);
        reset_command_pool(cmdpool);
        ++num_frame;
    }
}

HandleBatchedModel RendererVulkan::batch_model(const ImportedModel& model, BatchSettings settings) {
    const auto total_vertices = get_total_vertices();
    const auto total_indices = get_total_indices();
    const auto total_meshes = static_cast<uint32_t>(meshes.size());
    const auto total_materials = static_cast<uint32_t>(materials.size());
    const auto total_textures = static_cast<uint32_t>(textures.size());
    const auto total_models = static_cast<uint32_t>(models.size());
    const auto rt_metadata_handle = rt_metadata.emplace_back();

    flags |= RendererFlags::DIRTY_MODEL_BATCHES;

    Handle<RenderModel> model_handle = models.push_back(RenderModel{
        .flags = {},
        .rt_metadata = rt_metadata_handle,
        .first_vertex = total_vertices,
        .vertex_count = (uint32_t)model.vertices.size(),
        .first_index = total_indices,
        .index_count = (uint32_t)model.indices.size(),
        .first_mesh = total_meshes,
        .mesh_count = (uint32_t)model.meshes.size(),
        .first_material = total_materials,
        .material_count = (uint32_t)model.materials.size(),
        .first_texture = total_textures,
        .texture_count = (uint32_t)model.textures.size(),
    });

    if(settings.flags & BatchFlags::RAY_TRACED) {
        flags |= RendererFlags::DIRTY_BLAS;
        model_handle->flags |= RenderModelFlags::DIRTY_BLAS;
    }

    for(auto& mesh : model.meshes) {
        meshes.push_back(RenderMesh{ .vertex_offset = mesh.vertex_offset,
                                     .vertex_count = mesh.vertex_count,
                                     .index_offset = mesh.index_offset,
                                     .index_count = mesh.index_count,
                                     .material = mesh.material.value_or(0) });
    }

    for(auto& mat : model.materials) {
        materials.push_back(RenderMaterial{ .color_texture = mat.color_texture.value_or(0),
                                            .normal_texture = mat.normal_texture.value_or(0) });
    }

    VkSampler default_linear_sampler = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    for(auto& tex : model.textures) {
        textures.push_back(Image{ tex.name, tex.size.first, tex.size.second, 1, 1, 1, VK_FORMAT_R8G8B8A8_SRGB,
                                  VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
        upload_images.push_back(UploadImage{ .image_index = textures.size() - 1u, .rgba_data = tex.rgba_data });
    }

    upload_vertices.resize(upload_vertices.size() + model.vertices.size());
    upload_indices.resize(upload_indices.size() + model.indices.size());

    std::transform(model.vertices.begin(), model.vertices.end(), upload_vertices.end() - model.vertices.size(),
                   [&aabb = model_bbs.emplace_back()](const ImportedModel::Vertex& v) {
                       aabb.min = glm::min(aabb.min, v.pos);
                       aabb.max = glm::max(aabb.max, v.pos);
                       return Vertex{ .pos = v.pos, .nor = v.nor, .uv = v.uv };
                   });

    std::transform(model.indices.begin(), model.indices.end(), upload_indices.end() - model.indices.size(),
                   [](const auto& i) { return i; });

    // clang-format off
    ENG_LOG("Batching model {}: [VXS: {:.2f} KB, IXS: {:.2f} KB, Textures: {:.2f} KB]", 
            model.name,
            static_cast<float>(model.vertices.size() * sizeof(model.vertices[0])) / 1000.0f,
            static_cast<float>(model.indices.size() * sizeof(model.indices[0]) / 1000.0f),
            static_cast<float>(std::accumulate(model.textures.begin(), model.textures.end(), 0ull, [](uint64_t sum, const ImportedModel::Texture& tex) { return sum + tex.size.first * tex.size.second * 4u; })) / 1000.0f);
    // clang-format on

    return HandleBatchedModel{ *model_handle };
}

HandleInstancedModel RendererVulkan::instance_model(HandleBatchedModel model, InstanceSettings settings) {
    auto handle = model_instances.push_back(RenderModelInstance{ .model = Handle<RenderModel>{ *model },
                                                                 .flags = settings.flags,
                                                                 .transform = settings.transform,
                                                                 .tlas_instance_mask = settings.tlas_instance_mask });

    for(uint32_t i = 0; i < model->mesh_count; ++i) {
        mesh_instances.push_back(RenderMeshInstance{
            .model_instance = handle,
            .mesh = Handle<RenderMesh>{ model->first_mesh + i },
        });
    }

    flags |= RendererFlags::DIRTY_MODEL_INSTANCES;
    if(settings.flags & InstanceFlags::RAY_TRACED) { flags |= RendererFlags::DIRTY_TLAS; }

    return HandleInstancedModel{ *handle };
}

void RendererVulkan::upload_model_textures() {
    const auto total_tex_size =
        std::accumulate(upload_images.begin(), upload_images.end(), 0ull, [this](uint64_t sum, const UploadImage& tex) {
            return sum + textures.at(tex.image_index).width * textures.at(tex.image_index).height * 4ull;
        });

    Buffer texture_staging_buffer{ "texture staging buffer", total_tex_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true };

    std::vector<vks::CopyBufferToImageInfo2> texture_copy_datas;
    std::vector<vks::BufferImageCopy2> buffer_copies;

    texture_copy_datas.reserve(upload_images.size());
    buffer_copies.reserve(upload_images.size());

    uint64_t texture_byte_offset = 0;
    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    for(const auto& tex : upload_images) {
        auto& texture = textures.at(tex.image_index);

        vks::BufferImageCopy2& region = buffer_copies.emplace_back();
        region.bufferOffset = texture_byte_offset;
        region.imageExtent = { .width = texture.width, .height = texture.height, .depth = 1 };
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
        texture_byte_offset += texture.width * texture.height * 4ull;

        texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                  VK_ACCESS_2_TRANSFER_WRITE_BIT, true, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyBufferToImage2(cmd, &copy);
        texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                  false, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }

    end_recording(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkQueueWaitIdle(gq);
}
void RendererVulkan::upload_staged_models() {
    upload_model_textures();

    vertex_buffer.push_data(std::as_bytes(std::span{ upload_vertices }));
    index_buffer.push_data(std::as_bytes(std::span{ upload_indices }));

    upload_vertices = {};
    upload_indices = {};
}

void RendererVulkan::upload_instances() {
    upload_images = {};
    std::sort(model_instances.begin(), model_instances.end(),
              [](const RenderModelInstance& a, const RenderModelInstance& b) { return a.model < b.model; });
    std::sort(mesh_instances.begin(), mesh_instances.end(), [](const RenderMeshInstance& a, const RenderMeshInstance& b) {
        if(a.model_instance > b.model_instance) { return false; }
        if(a.mesh > b.mesh) { return false; }
        return true;
    });

    const auto total_triangles = get_total_triangles();
    std::vector<uint32_t> tlas_mesh_offsets;
    std::vector<uint32_t> blas_mesh_offsets;
    std::vector<uint32_t> triangle_mesh_ids;
    std::vector<uint32_t> mesh_instance_mesh_ids;
    std::vector<GPURenderMeshData> render_mesh_data;
    std::vector<VkDrawIndexedIndirectCommand> draw_commands;
    IndirectDrawCommandBufferHeader draw_header;
    std::vector<glm::mat4x3> tlas_transforms;
    std::vector<glm::mat4x3> mesh_instance_transforms;
    tlas_mesh_offsets.reserve(model_instances.size());
    blas_mesh_offsets.reserve(mesh_instances.size());
    triangle_mesh_ids.reserve(total_triangles);
    mesh_instance_mesh_ids.reserve(mesh_instances.size());
    render_mesh_data.reserve(meshes.size());
    draw_commands.reserve(mesh_instances.size());

    mesh_instance_transforms.reserve(mesh_instances.size());
    tlas_transforms.reserve(model_instances.size());

    for(const RenderModel& model : models) {
        for(uint32_t i = 0u; i < model.mesh_count; ++i) {
            const RenderMesh& mesh = meshes.at(model.first_mesh + i);
            const RenderMaterial& material = materials.at(model.first_material + mesh.material);

            render_mesh_data.push_back(GPURenderMeshData{
                .vertex_offset = model.first_vertex + mesh.vertex_offset,
                .index_offset = model.first_index + mesh.index_offset,
                .color_texture_idx = model.first_texture + material.color_texture,
            });

            for(uint32_t j = 0u; j < mesh.index_count / 3u; ++j) {
                triangle_mesh_ids.push_back(model.first_mesh + i);
            }

            blas_mesh_offsets.push_back((model.first_index + mesh.index_offset) / 3u);
        }
    }

    for(const RenderModelInstance& instance : model_instances) {
        tlas_mesh_offsets.push_back(instance.model->first_mesh);
        tlas_transforms.push_back(instance.transform);
    }

    {
        const RenderMeshInstance* mi = &mesh_instances.at(0);
        VkDrawIndexedIndirectCommand* cmd = &draw_commands.emplace_back();
        cmd->firstIndex = mi->model_instance->model->first_index + mi->mesh->index_offset;
        cmd->indexCount = mi->mesh->index_count;
        cmd->instanceCount = 1;
        cmd->vertexOffset = mi->model_instance->model->first_vertex + mi->mesh->vertex_offset;
        mesh_instance_mesh_ids.push_back(*mi->mesh);
        mesh_instance_transforms.push_back(mi->model_instance->transform);
        for(uint32_t i = 1; i < mesh_instances.size(); ++i) {
            const RenderMeshInstance& cmi = mesh_instances.at(i);

            if(mi->mesh != cmi.mesh) {
                mi = &cmi;
                cmd = &draw_commands.emplace_back();
                cmd->indexCount = mi->mesh->index_count;
                cmd->instanceCount = 1;
                cmd->firstIndex = mi->mesh->index_offset + mi->model_instance->model->first_index;
                cmd->firstInstance = i;
                cmd->vertexOffset = mi->model_instance->model->first_vertex + mi->mesh->vertex_offset;
            } else {
                ++cmd->instanceCount;
            }

            mesh_instance_mesh_ids.push_back(*mi->mesh);
            mesh_instance_transforms.push_back(mi->model_instance->transform);
        }

        draw_header.draw_count = draw_commands.size();
        draw_header.mesh_instance_count = mesh_instances.size();
    }

    // clang-format off
    indirect_draw_buffer = Buffer{"indirect draw", sizeof(IndirectDrawCommandBufferHeader) + draw_commands.size() * sizeof(draw_commands[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false};
    indirect_draw_buffer.push_data(&draw_header, sizeof(IndirectDrawCommandBufferHeader));
    indirect_draw_buffer.push_data(draw_commands);

    mesh_instance_transform_buffer[0] = new Buffer{"mesh instance transforms 1", mesh_instance_transforms.size() * sizeof(mesh_instance_transforms[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true};
    mesh_instance_transform_buffer[1] = new Buffer{"mesh instance transforms 0", mesh_instance_transforms.size() * sizeof(mesh_instance_transforms[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true};
    mesh_instance_transform_buffer[0]->push_data(mesh_instance_transforms);
    mesh_instance_transform_buffer[1]->push_data(mesh_instance_transforms);

    mesh_instance_mesh_id_buffer = Buffer{"mesh instance mesh id", mesh_instance_mesh_ids.size() * sizeof(mesh_instance_mesh_ids[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    mesh_instance_mesh_id_buffer.push_data(mesh_instance_mesh_ids);

    tlas_mesh_offsets_buffer = Buffer{"tlas mesh offsets", tlas_mesh_offsets.size() * sizeof(tlas_mesh_offsets[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    tlas_mesh_offsets_buffer.push_data(tlas_mesh_offsets);
    
    tlas_transform_buffer = Buffer{"tlas transform", tlas_transforms.size() * sizeof(tlas_transforms[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    tlas_transform_buffer.push_data(tlas_transforms);

    blas_mesh_offsets_buffer = Buffer{"blas mesh offsets", blas_mesh_offsets.size() * sizeof(blas_mesh_offsets[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    blas_mesh_offsets_buffer.push_data(blas_mesh_offsets);

    triangle_mesh_ids_buffer = Buffer{"triangle mesh id buffer", triangle_mesh_ids.size() * sizeof(triangle_mesh_ids[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    triangle_mesh_ids_buffer.push_data(triangle_mesh_ids);

    mesh_datas_buffer = Buffer{"render mesh data", render_mesh_data.size() * sizeof(render_mesh_data[0]), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false};
    mesh_datas_buffer.push_data(render_mesh_data);
    // clang-format on
}

void RendererVulkan::upload_transforms() {
    std::vector<glm::mat4x3> transforms;
    transforms.reserve(mesh_instances.size());

    std::transform(mesh_instances.begin(), mesh_instances.end(), std::back_inserter(transforms),
                   [this](const RenderMeshInstance& inst) { return inst.model_instance->transform; });

    mesh_instance_transform_buffer[1]->push_data(transforms, 0);
    std::swap(mesh_instance_transform_buffer[0], mesh_instance_transform_buffer[1]);
}

void RendererVulkan::update_transform(HandleInstancedModel model, glm::mat4x3 transform) {
    model->transform = transform;
    // ENG_WARN("FIX THIS METHOD");
    //  TODO: this can index out of bounds if the instance is new and upload_instances didn't run yet (the buffer was not resized)
    //  memcpy(static_cast<glm::mat4x3*>(per_tlas_transform_buffer.mapped) + model_instances.index(handle), &transform,
    //        sizeof(transform));
    if(model->flags & InstanceFlags::RAY_TRACED) { flags |= RendererFlags::REFIT_TLAS; }
    upload_positions.emplace_back(Handle<RenderModelInstance>{ *model }, transform);
    flags |= RendererFlags::UPDATE_MESH_POSITIONS;
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

    RendererPipelineLayout default_layout = RendererPipelineLayoutBuilder{}
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
    RendererPipelineLayout imgui_layout =
        RendererPipelineLayoutBuilder{}
            .add_set_binding(0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, {}, VK_SHADER_STAGE_FRAGMENT_BIT)
            .set_push_constants(16, VK_SHADER_STAGE_VERTEX_BIT)
            .build();

    pipelines[RendererPipelineType::DEFAULT_UNLIT] = RendererPipelineWrapper{
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

    pipelines[RendererPipelineType::DDGI_PROBE_RAYCAST] = RendererPipelineWrapper{
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

    pipelines[RendererPipelineType::DDGI_PROBE_UPDATE] = RendererPipelineWrapper{
        .pipeline = RendererComputePipelineBuilder{}
                        .set_layout(default_layout.layout)
                        .set_stage(shader_modules.at(ShaderModuleType::RT_BASIC_PROBE_IRRADIANCE_COMPUTE).module)
                        .build(),
        .layout = default_layout.layout,
    };

    pipelines[RendererPipelineType::DDGI_PROBE_OFFSET] = RendererPipelineWrapper{
        .pipeline = RendererComputePipelineBuilder{}
                        .set_layout(default_layout.layout)
                        .set_stage(shader_modules.at(ShaderModuleType::RT_BASIC_PROBE_PROBE_OFFSET_COMPUTE).module)
                        .build(),
        .layout = default_layout.layout,
    };

    layouts.push_back(default_layout);
    layouts.push_back(imgui_layout);

    per_frame_desc_pool = descriptor_pool_allocator.allocate_pool(default_layout, 0, 2);
    imgui_desc_pool = descriptor_pool_allocator.allocate_pool(imgui_layout, 0, 1);

    for(int i = 0; i < 2; ++i) {
        depth_buffers[i] = Image{
            std::format("depth_buffer_{}", i), Engine::window()->size[0], Engine::window()->size[1], 1, 1, 1, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        };
    }
}

void RendererVulkan::build_sbt() {
    const RendererPipelineWrapper& pipeline = pipelines.at(RendererPipelineType::DDGI_PROBE_RAYCAST);
    const uint32_t handleSize = rt_props.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = align_up(rt_props.shaderGroupHandleSize, rt_props.shaderGroupHandleAlignment);
    const uint32_t groupCount = static_cast<uint32_t>(pipeline.rt_shader_group_count);
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    vkGetRayTracingShaderGroupHandlesKHR(dev, pipeline.pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data());

    const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    sbt = Buffer{ "buffer_sbt", sbtSize, bufferUsageFlags, false };
    sbt.push_data(shaderHandleStorage);
}

void RendererVulkan::create_rt_output_image() {
    const auto* window = Engine::window();
    rt_image = Image{
        "rt_image", window->size[0], window->size[1], 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT
    };

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    rt_image.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                               VK_ACCESS_2_SHADER_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);
    end_recording(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkQueueWaitIdle(gq);
}

void RendererVulkan::build_blas() {
    std::vector<const RenderModel*> dirty_models;
    std::vector<Handle<RenderModelRTMetadata>> dirty_rt_metadatas;
    uint32_t total_dirty_meshes = 0;

    // TODO: Right now it is wrong to iterate over elements inside
    // HandleVector, if any element was removed from it.
    for(auto& model : models) {
        if(model.flags & RenderModelFlags::DIRTY_BLAS) {
            // TODO: Maybe move it to the end, when the building is finished
            dirty_models.push_back(&model);
            dirty_rt_metadatas.push_back(model.rt_metadata);
            total_dirty_meshes += model.mesh_count;
            model.flags ^= RenderModelFlags::DIRTY_BLAS;
        }
    }

    vks::AccelerationStructureGeometryTrianglesDataKHR triangles;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex_buffer.bda;
    triangles.vertexStride = sizeof(Vertex);
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = index_buffer.bda;
    triangles.maxVertex = get_total_vertices() - 1u;

    std::vector<vks::AccelerationStructureGeometryKHR> geometries;
    std::vector<vks::AccelerationStructureBuildGeometryInfoKHR> build_geometries;
    std::vector<uint32_t> scratch_sizes;
    std::vector<vks::AccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<uint32_t> max_primitive_counts;
    Buffer scratch_buffer;

    geometries.reserve(total_dirty_meshes);
    build_geometries.reserve(dirty_models.size());
    scratch_sizes.reserve(dirty_models.size());
    max_primitive_counts.reserve(total_dirty_meshes);
    ranges.reserve(total_dirty_meshes);

    for(uint32_t i = 0; i < dirty_models.size(); ++i) {
        const RenderModel& model = *dirty_models.at(i);
        Handle<RenderModelRTMetadata> meta = dirty_rt_metadatas.at(i);

        for(uint32_t j = 0; j < model.mesh_count; ++j) {
            const RenderMesh& mesh = meshes.at(model.first_mesh + j);
            vks::AccelerationStructureGeometryKHR& geometry = geometries.emplace_back();
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            geometry.geometry.triangles = triangles;
            max_primitive_counts.push_back(mesh.index_count / 3u);
        }

        vks::AccelerationStructureBuildGeometryInfoKHR& build_geometry = build_geometries.emplace_back();
        build_geometry.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_geometry.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        build_geometry.geometryCount = model.mesh_count;
        build_geometry.pGeometries = &geometries.at(geometries.size() - model.mesh_count);
        build_geometry.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        vks::AccelerationStructureBuildSizesInfoKHR build_size_info;
        vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_geometry,
                                                &max_primitive_counts.at(max_primitive_counts.size() - model.mesh_count),
                                                &build_size_info);

        meta->blas_buffer =
            Buffer{ "blas_buffer", build_size_info.accelerationStructureSize,
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
        scratch_sizes.push_back(align_up(build_size_info.buildScratchSize,
                                         static_cast<VkDeviceSize>(rt_acc_props.minAccelerationStructureScratchOffsetAlignment)));

        vks::AccelerationStructureCreateInfoKHR blas_info;
        blas_info.buffer = meta->blas_buffer.buffer;
        blas_info.size = build_size_info.accelerationStructureSize;
        blas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VK_CHECK(vkCreateAccelerationStructureKHR(dev, &blas_info, nullptr, &meta->blas));
    }

    const auto total_scratch_size = std::accumulate(scratch_sizes.begin(), scratch_sizes.end(), 0ul);
    scratch_buffer = Buffer{ "blas_scratch_buffer", total_scratch_size, rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };

    for(uint32_t i = 0, offset = 0; i < dirty_models.size(); ++i) {
        const RenderModel& model = *dirty_models.at(i);
        Handle<RenderModelRTMetadata> meta = dirty_rt_metadatas.at(i);

        build_geometries.at(i).scratchData.deviceAddress = scratch_buffer.bda + offset;
        build_geometries.at(i).dstAccelerationStructure = meta->blas;
        offset += scratch_sizes.at(i);

        for(uint32_t j = 0; j < model.mesh_count; ++j) {
            const RenderMesh& mesh = meshes.at(model.first_mesh + j);
            VkAccelerationStructureBuildRangeInfoKHR& range = ranges.emplace_back();
            range.firstVertex = model.first_vertex + mesh.vertex_offset;
            range.primitiveCount = mesh.index_count / 3u;
            range.primitiveOffset = (model.first_index + mesh.index_offset) * sizeof(model.first_index);
            range.transformOffset = 0;
        }
    }

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> poffsets(dirty_models.size());
    for(uint32_t i = 0, offset = 0; i < dirty_models.size(); ++i) {
        poffsets.at(i) = &ranges.at(offset);
        offset += dirty_models.at(i)->mesh_count;
    }
    vkCmdBuildAccelerationStructuresKHR(cmd, build_geometries.size(), build_geometries.data(), poffsets.data());
    end_recording(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkDeviceWaitIdle(dev);
}

void RendererVulkan::build_tlas() {
    std::vector<const RenderModelInstance*> render_model_instances;
    render_model_instances.reserve(model_instances.size());

    for(auto& i : model_instances) {
        if(i.flags & InstanceFlags::RAY_TRACED) { render_model_instances.push_back(&i); }
    }

    std::vector<vks::AccelerationStructureInstanceKHR> instances(render_model_instances.size());
    for(uint32_t i = 0; i < instances.size(); ++i) {
        auto& instance = instances.at(i);
        instance.transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(render_model_instances.at(i)->transform));
        instance.instanceCustomIndex = 0;
        instance.mask = render_model_instances.at(i)->tlas_instance_mask;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = render_model_instances.at(i)->model->rt_metadata->blas_buffer.bda;
    }

    tlas_instance_buffer =
        Buffer{ "tlas_instance_buffer", sizeof(instances[0]) * instances.size(), 16u,
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                false };
    tlas_instance_buffer.push_data(instances);

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

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
    end_recording(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkDeviceWaitIdle(dev);

    tlas_buffer = std::move(buffer_tlas);
}

void RendererVulkan::refit_tlas() {
    std::vector<const RenderModelInstance*> render_model_instances;
    render_model_instances.reserve(model_instances.size());

    for(auto& i : model_instances) {
        if(i.flags & InstanceFlags::RAY_TRACED) { render_model_instances.push_back(&i); }
    }

    std::vector<vks::AccelerationStructureInstanceKHR> instances(render_model_instances.size());
    for(uint32_t i = 0; i < instances.size(); ++i) {
        auto& instance = instances.at(i);
        instance.transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(render_model_instances.at(i)->transform));
        instance.instanceCustomIndex = 0;
        instance.mask = render_model_instances.at(i)->tlas_instance_mask;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = render_model_instances.at(i)->model->rt_metadata->blas_buffer.bda;
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

    const uint32_t max_primitives = instances.size();

    vks::AccelerationStructureBuildRangeInfoKHR build_range;
    build_range.primitiveCount = max_primitives;
    build_range.primitiveOffset = 0;
    build_range.firstVertex = 0;
    build_range.transformOffset = 0;
    VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };

    auto cmd = begin_recording(cmdpool, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
    end_recording(cmd);
    scheduler_gq.enqueue_wait_submit({ { cmd } });
    vkDeviceWaitIdle(dev);
}

void RendererVulkan::prepare_ddgi() {
    for(auto& mi : model_instances) {
        BoundingBox aabb{ .min = model_bbs.at(*mi.model).min * mi.transform, .max = model_bbs.at(*mi.model).max * mi.transform };
        scene_bounding_box.min = aabb.min;
        scene_bounding_box.max = aabb.max;
    }

    ddgi.probe_dims = scene_bounding_box;
    ddgi.probe_distance = 0.3f;
    ddgi.probe_dims.min *= glm::vec3{ 0.9, 0.7, 0.9 };
    ddgi.probe_dims.max *= glm::vec3{ 0.9, 0.7, 0.9 };

    ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
    ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
                          std::bit_ceil(ddgi.probe_counts.z) };
    const auto num_probes = ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z;
    // ddgi.probe_counts = { 16, 4, 16 };

    ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };

    const uint32_t irradiance_texture_width = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const uint32_t irradiance_texture_height = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.z;
    const uint32_t visibility_texture_width = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
    const uint32_t visibility_texture_height = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.z;

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

    ddgi.probe_offsets_texture.transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                                                 VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                                                 VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, true, VK_IMAGE_LAYOUT_GENERAL);

    end_recording(cmd);
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

VkCommandBuffer RendererVulkan::begin_recording(VkCommandPool pool, VkCommandBufferUsageFlags usage) {
    VkCommandBuffer cmd;

    auto free_buffer_it =
        std::find_if(command_buffers.begin(), command_buffers.end(), [](const CommandBuffer& b) { return !b.used; });
    if(free_buffer_it == command_buffers.end()) {
        vks::CommandBufferAllocateInfo alloc_info;
        alloc_info.commandPool = pool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_CHECK(vkAllocateCommandBuffers(dev, &alloc_info, &cmd));
        command_buffers.emplace_back(cmd, true);
    } else {
        cmd = free_buffer_it->command_buffer;
        free_buffer_it->used = true;
    }

    vks::CommandBufferBeginInfo info;
    info.flags = usage;
    VK_CHECK(vkBeginCommandBuffer(cmd, &info));

    return cmd;
}

void RendererVulkan::end_recording(VkCommandBuffer buffer) { VK_CHECK(vkEndCommandBuffer(buffer)); }

void RendererVulkan::reset_command_pool(VkCommandPool pool) {
    VK_CHECK(vkResetCommandPool(dev, pool, {}));
    for(auto& e : command_buffers) {
        e.used = false;
    }
}

Image::Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers,
             VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage)
    : format(format), mips(mips), layers(layers), width(width), height(height), depth(depth) {
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

    VK_CHECK(vmaCreateImage(get_renderer().vma, &iinfo, &vmainfo, &image, &alloc, nullptr));

    _deduce_aspect(usage);
    _create_default_view(dims, usage);

    set_debug_name(image, name);
    set_debug_name(view, std::format("{}_default_view", name));
}

Image::Image(const std::string& name, VkImage image, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips,
             uint32_t layers, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage)
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

RendererPipelineLayout RendererPipelineLayoutBuilder::build() {
    std::vector<VkDescriptorSetLayout> vk_layouts;
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> bindings;
    std::vector<std::vector<VkDescriptorBindingFlags>> binding_flags;
    for(uint32_t i = 0; i < descriptor_layouts.size(); ++i) {
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

    return RendererPipelineLayout{ layout,
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

DescriptorSetWriter& DescriptorSetWriter::write(uint32_t binding, uint32_t array_element, const Image& image, VkImageLayout layout) {
    writes.emplace_back(binding, array_element, WriteImage{ image.view, layout, VkSampler{} });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(uint32_t binding, uint32_t array_element, const Image& image,
                                                VkSampler sampler, VkImageLayout layout) {
    writes.emplace_back(binding, array_element, WriteImage{ image.view, layout, sampler });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(uint32_t binding, uint32_t array_element, VkImageView image,
                                                VkSampler sampler, VkImageLayout layout) {
    writes.emplace_back(binding, array_element, WriteImage{ image, layout, sampler });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(uint32_t binding, uint32_t array_element, const Buffer& buffer,
                                                uint32_t offset, uint32_t range) {
    writes.emplace_back(binding, array_element, WriteBuffer{ buffer.buffer, offset, range });
    return *this;
}

DescriptorSetWriter& DescriptorSetWriter::write(uint32_t binding, uint32_t array_element, const VkAccelerationStructureKHR ac) {
    writes.emplace_back(binding, array_element, ac);
    return *this;
}

bool DescriptorSetWriter::update(VkDescriptorSet set, const RendererPipelineLayout& layout, uint32_t set_idx) {
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

VkDescriptorPool DescriptorPoolAllocator::allocate_pool(const RendererPipelineLayout& layout, uint32_t set,
                                                        uint32_t max_sets, VkDescriptorPoolCreateFlags flags) {
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

    for(uint32_t i = 0; i < bindings.size(); ++i) {
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
    VkDescriptorPool& pool = pools.emplace_back();
    VK_CHECK(vkCreateDescriptorPool(get_renderer().dev, &info, nullptr, &pool));
    return pool;
}

VkDescriptorSet DescriptorPoolAllocator::allocate_set(VkDescriptorPool pool, VkDescriptorSetLayout layout, uint32_t variable_count) {
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

    return set;
}

void DescriptorPoolAllocator::reset_pool(VkDescriptorPool pool) {
    VK_CHECK(vkResetDescriptorPool(get_renderer().dev, pool, {}));
}

ThreadedQueueScheduler::ThreadedQueueScheduler(VkQueue queue) : scheduler{ queue } {
    assert(queue);
    submit_thread = std::jthread{ &ThreadedQueueScheduler::submit_thread_func, this };
    submit_thread_stop_token = submit_thread.get_stop_token();
}

ThreadedQueueScheduler::ThreadedQueueScheduler(ThreadedQueueScheduler&& other) noexcept { *this = std::move(other); }

ThreadedQueueScheduler& ThreadedQueueScheduler::operator=(ThreadedQueueScheduler&& other) noexcept {
    if(scheduler.vkqueue) {
        if(submit_thread_stop_token.stop_possible()) { submit_thread.request_stop(); }
        submit_thread.join();
    }

    scheduler = std::move(other.scheduler);
    submit_queue = std::move(other.submit_queue);
    submit_thread = std::jthread{ &ThreadedQueueScheduler::submit_thread_func, this };
    submit_thread_stop_token = submit_thread.get_stop_token();
    return *this;
}

ThreadedQueueScheduler::~ThreadedQueueScheduler() noexcept { submit_thread_cvar.notify_one(); }

std::shared_ptr<std::latch> ThreadedQueueScheduler::enqueue(const RecordingSubmitInfo& info, VkFence fence) {
    std::scoped_lock lock{ submit_queue_mutex };
    submit_queue.push_back(QueueItem{ info, fence, std::make_shared<std::latch>(1) });
    submit_thread_cvar.notify_one();
    return submit_queue.back().on_submit_latch;
}

void ThreadedQueueScheduler::enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence) {
    enqueue(info, fence)->wait();
}

void ThreadedQueueScheduler::submit_thread_func() {
    while(!submit_thread_stop_token.stop_requested()) {
        std::unique_lock lock{ submit_queue_mutex };
        submit_thread_cvar.wait(lock,
                                [this]() { return !submit_queue.empty() || submit_thread_stop_token.stop_requested(); });

        if(submit_thread_stop_token.stop_requested()) {
            lock.unlock();
            return;
        }

        std::vector<QueueItem> submits = std::move(submit_queue);
        submit_queue.clear();
        lock.unlock();

        for(uint32_t i = 0; i < submits.size(); ++i) {
            scheduler.enqueue(submits.at(i).info, submits.at(i).fence);
            submits.at(i).on_submit_latch->count_down();
        }
    }

    ENG_LOG("Threaded queue scheduler safely stopping");
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
                               .waitSemaphoreInfoCount = (uint32_t)batch.info_waits.size(),
                               .pWaitSemaphoreInfos = batch.info_waits.data(),
                               .commandBufferInfoCount = (uint32_t)batch.info_submits.size(),
                               .pCommandBufferInfos = batch.info_submits.data(),
                               .signalSemaphoreInfoCount = (uint32_t)batch.info_signals.size(),
                               .pSignalSemaphoreInfos = batch.info_signals.data() };
    VK_CHECK(vkQueueSubmit2(vkqueue, 1, &submit_info, fence));
}

void QueueScheduler::enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence) { enqueue(info, fence); }
