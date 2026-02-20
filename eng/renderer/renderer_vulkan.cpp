#include <filesystem>
#include <bitset>
#include <numeric>
#include <fstream>
#include <utility>
#include <chrono>
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
#include <eng/ecs/ecs.hpp>
#include <eng/ecs/components.hpp>
#include <eng/engine.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/imgui/imgui_renderer.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <assets/shaders/bindless_structures.glsli>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/common/to_vk.hpp>
#include <eng/common/paths.hpp>
#include <eng/common/to_string.hpp>
#include <eng/camera.hpp>
#include <eng/renderer/set_debug_name.hpp>

// https://www.shadertoy.com/view/WlSSWc
// static float halton(int i, int b)
//{
//    /* Creates a halton sequence of values between 0 and 1.
//        https://en.wikipedia.org/wiki/Halton_sequence
//        Used for jittering based on a constant set of 2D points. */
//    float f = 1.0;
//    float r = 0.0;
//    while(i > 0)
//    {
//        f = f / float(b);
//        r = r + f * float(i % b);
//        i = i / b;
//    }
//
//    return r;
//}

namespace eng
{

namespace gfx
{

void DescriptorLayoutMetadataVk::init(DescriptorLayout& a)
{
    if(a.md.vk) { return; }
    auto* md = new DescriptorLayoutMetadataVk{};
    a.md.vk = md;

    std::vector<VkSampler> vkimsamps;
    std::vector<VkDescriptorBindingFlags> vkbindingflags;
    const auto vkbindings = [&a, &vkimsamps, &vkbindingflags] {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(a.layout.size());
        vkbindingflags.reserve(a.layout.size());
        for(const auto& e : a.layout)
        {
            if(e.immutable_samplers)
            {
                vkimsamps.resize(e.size);
                for(auto i = 0u; i < e.size; ++i)
                {
                    vkimsamps[i] = e.immutable_samplers[i]->md.as_vk()->sampler;
                }
            }
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding = e.slot,
                .descriptorType = to_vk(e.type),
                .descriptorCount = e.size,
                .stageFlags = to_vk(e.stages),
                .pImmutableSamplers = e.immutable_samplers ? vkimsamps.data() : nullptr,
            });
            vkbindingflags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                                     VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT);
        }
        return bindings;
    }();

    const auto vkbindingflagsinfo = Vks(VkDescriptorSetLayoutBindingFlagsCreateInfo{
        .bindingCount = (uint32_t)vkbindingflags.size(),
        .pBindingFlags = vkbindingflags.data(),
    });

    const auto vklayoutinfo = Vks(VkDescriptorSetLayoutCreateInfo{
        .pNext = &vkbindingflagsinfo,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = (uint32_t)vkbindings.size(),
        .pBindings = vkbindings.data(),
    });
    VK_CHECK(vkCreateDescriptorSetLayout(RendererBackendVk::get_dev(), &vklayoutinfo, nullptr, &md->layout));
}

void DescriptorLayoutMetadataVk::destroy(DescriptorLayout& a)
{
    if(!a.md.vk) { return; }
    vkDestroyDescriptorSetLayout(RendererBackendVk::get_dev(), a.md.vk->layout, nullptr);
    delete a.md.vk;
    a.md.vk = nullptr;
}

void PipelineLayoutMetadataVk::init(PipelineLayout& a)
{
    if(a.md.vk) { return; }
    auto* md = new PipelineLayoutMetadataVk{};
    a.md.vk = md;

    std::vector<VkDescriptorSetLayout> vksls(a.layout.size());
    for(auto i = 0u; i < vksls.size(); ++i)
    {
        vksls[i] = a.layout[i]->md.vk->layout;
    }
    VkPushConstantRange range{ .stageFlags = gfx::to_vk(a.push_range.stages), .offset = 0ull, .size = a.push_range.size };

    const auto pli = Vks(VkPipelineLayoutCreateInfo{ .setLayoutCount = (uint32_t)vksls.size(),
                                                     .pSetLayouts = vksls.data(),
                                                     .pushConstantRangeCount = range.size > 0 ? 1u : 0u,
                                                     .pPushConstantRanges = &range });
    VK_CHECK(vkCreatePipelineLayout(RendererBackendVk::get_dev(), &pli, nullptr, &md->layout));
}

void PipelineLayoutMetadataVk::destroy(PipelineLayout& a)
{
    // destroy layout
    // free metadata
    ENG_ASSERT(false);
}

void PipelineMetadataVk::init(const Pipeline& a)
{
    if(!a.md.vk)
    {
        ENG_ERROR("Pipeline metadata null.");
        return;
    }

    if(!a.info.layout)
    {
        ENG_ASSERT(false, "No pipeline layout");
        return;
    }

    auto vkdev = RendererBackendVk::get_dev();
    auto* md = a.md.vk;

    if(md->pipeline) { vkDestroyPipeline(vkdev, md->pipeline, nullptr); }

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(a.info.shaders.size());
    for(const auto& e : a.info.shaders)
    {
        stages.push_back(Vks(VkPipelineShaderStageCreateInfo{ .stage = gfx::to_vk(e->stage), .module = e->md.vk->shader, .pName = "main" }));
    }

    if(a.type == PipelineType::COMPUTE)
    {
        const auto vkinfo = Vks(VkComputePipelineCreateInfo{ .stage = stages.at(0), .layout = a.info.layout->md.vk->layout });
        VK_CHECK(vkCreateComputePipelines(vkdev, {}, 1, &vkinfo, {}, &md->pipeline));
        return;
    }

    std::vector<VkVertexInputBindingDescription> vkbindings(a.info.bindings.size());
    for(auto i = 0u; i < a.info.bindings.size(); ++i)
    {
        vkbindings.at(i) = { a.info.bindings.at(i).binding, a.info.bindings.at(i).stride,
                             a.info.bindings.at(i).instanced ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX };
    }
    std::vector<VkVertexInputAttributeDescription> vkattributes(a.info.attributes.size());
    for(auto i = 0u; i < a.info.attributes.size(); ++i)
    {
        vkattributes.at(i) = { a.info.attributes.at(i).location, a.info.attributes.at(i).binding,
                               gfx::to_vk(a.info.attributes.at(i).format), a.info.attributes.at(i).offset };
    }
    auto pVertexInputState =
        Vks(VkPipelineVertexInputStateCreateInfo{ .vertexBindingDescriptionCount = (uint32_t)a.info.bindings.size(),
                                                  .pVertexBindingDescriptions = vkbindings.data(),
                                                  .vertexAttributeDescriptionCount = (uint32_t)a.info.attributes.size(),
                                                  .pVertexAttributeDescriptions = vkattributes.data() });

    auto pInputAssemblyState = Vks(VkPipelineInputAssemblyStateCreateInfo{ .topology = gfx::to_vk(a.info.topology) });

    auto pTessellationState = Vks(VkPipelineTessellationStateCreateInfo{});

    auto pViewportState = Vks(VkPipelineViewportStateCreateInfo{});

    auto pRasterizationState = Vks(VkPipelineRasterizationStateCreateInfo{
        .polygonMode = gfx::to_vk(a.info.polygon_mode),
        .cullMode = gfx::to_vk(a.info.culling),
        .frontFace = a.info.front_is_ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = a.info.line_width,
    });

    auto pMultisampleState = Vks(VkPipelineMultisampleStateCreateInfo{
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    });

    // vkstencil
    auto pDepthStencilState = Vks(VkPipelineDepthStencilStateCreateInfo{
            .depthTestEnable = a.info.depth_test,
            .depthWriteEnable = a.info.depth_write,
            .depthCompareOp = gfx::to_vk(a.info.depth_compare),
            .depthBoundsTestEnable = false,
            .stencilTestEnable = a.info.stencil_test,
            .front = {
                gfx::to_vk(a.info.stencil_front.fail),
                gfx::to_vk(a.info.stencil_front.pass),
                gfx::to_vk(a.info.stencil_front.depth_fail),
                gfx::to_vk(a.info.stencil_front.compare),
                a.info.stencil_front.compare_mask,
                a.info.stencil_front.write_mask,
                a.info.stencil_front.ref,
            },
            .back = {
                gfx::to_vk(a.info.stencil_back.fail),
                gfx::to_vk(a.info.stencil_back.pass),
                gfx::to_vk(a.info.stencil_back.depth_fail),
                gfx::to_vk(a.info.stencil_back.compare),
                a.info.stencil_back.compare_mask,
                a.info.stencil_back.write_mask,
                a.info.stencil_back.ref,
            },
        });

    std::array<VkPipelineColorBlendAttachmentState, 8> vkblends;
    std::array<VkFormat, 8> vkcol_formats;
    for(uint32_t i = 0; i < a.info.attachments.count; ++i)
    {
        vkblends.at(i) = { a.info.attachments.blend_states.at(i).enable,
                           gfx::to_vk(a.info.attachments.blend_states.at(i).src_color_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).dst_color_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).color_op),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).src_alpha_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).dst_alpha_factor),
                           gfx::to_vk(a.info.attachments.blend_states.at(i).alpha_op),
                           VkColorComponentFlags{ ((uint32_t)a.info.attachments.blend_states.at(i).r) << 0 |
                                                  ((uint32_t)a.info.attachments.blend_states.at(i).g) << 1 |
                                                  ((uint32_t)a.info.attachments.blend_states.at(i).b) << 2 |
                                                  ((uint32_t)a.info.attachments.blend_states.at(i).a) << 3 } };
        vkcol_formats.at(i) = gfx::to_vk(a.info.attachments.color_formats.at(i));
    }
    auto pColorBlendState = Vks(VkPipelineColorBlendStateCreateInfo{
        .attachmentCount = a.info.attachments.count,
        .pAttachments = vkblends.data(),
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
        .colorAttachmentCount = a.info.attachments.count,
        .pColorAttachmentFormats = vkcol_formats.data(),
        .depthAttachmentFormat = gfx::to_vk(a.info.attachments.depth_format),
        .stencilAttachmentFormat = gfx::to_vk(a.info.attachments.stencil_format),
    });

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
        .layout = a.info.layout->md.vk->layout,
    });
    VK_CHECK(vkCreateGraphicsPipelines(vkdev, nullptr, 1, &vk_info, nullptr, &md->pipeline));
}

void PipelineMetadataVk::destroy(Pipeline& a)
{
    if(!a.md.vk) { return; }
    auto* md = a.md.vk;
    ENG_ASSERT(md->pipeline);
    vkDestroyPipeline(RendererBackendVk::get_dev(), md->pipeline, nullptr);
    delete a.md.vk;
    a.md.vk = nullptr;
}

void BufferMetadataVk::init(Buffer& a, AllocateMemory allocate)
{
    if(a.md.as_vk())
    {
        ENG_ERROR("Trying to init already init buffer");
        return;
    }

    auto* md = new BufferMetadataVk{};
    a.md.ptr = md;
    const auto cpu_map = a.usage.test(gfx::BufferUsage::CPU_ACCESS);
    if(a.capacity == 0)
    {
        ENG_WARN("Capacity cannot be 0");
        return;
    }
    a.usage |= BufferUsage::TRANSFER_SRC_BIT | BufferUsage::TRANSFER_DST_BIT;

    auto& backend = RendererBackendVk::get_instance();
    VkBufferCreateInfo vkinfo;
    VmaAllocationInfo vmaai{};
    init(a, vkinfo);
    const auto vmainfo = VmaAllocationCreateInfo{
        .flags = cpu_map ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    if(allocate == AllocateMemory::ALIASED) { VK_CHECK(vkCreateBuffer(backend.dev, &vkinfo, nullptr, &md->buffer)); }
    else { VK_CHECK(vmaCreateBuffer(backend.vma, &vkinfo, &vmainfo, &md->buffer, &md->vma_alloc, &vmaai)); }

    if(!md->buffer)
    {
        ENG_WARN("Could not create buffer");
        return;
    }
    if(cpu_map) { a.memory = vmaai.pMappedData; }
    if(vkinfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const auto vkbdai = Vks(VkBufferDeviceAddressInfo{ .buffer = md->buffer });
        md->device_address = vkGetBufferDeviceAddress(backend.dev, &vkbdai);
    }
}

void BufferMetadataVk::init(const Buffer& a, VkBufferCreateInfo& info)
{
    info = Vks(VkBufferCreateInfo{ .size = a.capacity, .usage = to_vk(a.usage) });
}

void BufferMetadataVk::destroy(Buffer& a)
{
    if(!a.md.as_vk())
    {
        ENG_ASSERT(a.capacity == 0);
        return;
    }
    auto& backend = RendererBackendVk::get_instance();
    auto* md = a.md.as_vk();
    if(!md) { return; }
    if(!md->is_aliased) { vmaDestroyBuffer(backend.vma, md->buffer, md->vma_alloc); }
    else { vkDestroyBuffer(backend.dev, md->buffer, nullptr); }
    delete md;
    a.md.ptr = nullptr;
}

void ImageMetadataVk::init(Image& a, AllocateMemory allocate, void* user_data)
{
    if(a.md.as_vk())
    {
        ENG_ERROR("Trying to init already init image");
        return;
    }

    auto& backend = RendererBackendVk::get_instance();
    auto* md = new ImageMetadataVk{};
    a.md.ptr = md;

    if(a.width + a.height + a.depth == 0)
    {
        ENG_WARN("Trying to create 0-sized image");
        return;
    }

    VmaAllocationCreateInfo vma_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    VkImageCreateInfo info;
    init(a, info);
    if(allocate == AllocateMemory::EXTERNAL)
    {
        ENG_ASSERT(user_data);
        md->image = static_cast<VkImage>(user_data);
    }
    else if(allocate == AllocateMemory::ALIASED) { VK_CHECK(vkCreateImage(backend.dev, &info, nullptr, &md->image)); }
    else if(allocate == AllocateMemory::YES)
    {
        VK_CHECK(vmaCreateImage(backend.vma, &info, &vma_info, &md->image, &md->vmaa, nullptr));
    }
    if(!md->image) { ENG_ERROR("Could not create image"); }
}

void ImageMetadataVk::init(Image& a, VkImageCreateInfo& info)
{
    info = Vks(VkImageCreateInfo{ .imageType = to_vk(a.type),
                                  .format = to_vk(a.format),
                                  .extent = { a.width, a.height, a.depth },
                                  .mipLevels = a.mips,
                                  .arrayLayers = a.layers,
                                  .samples = VK_SAMPLE_COUNT_1_BIT,
                                  .tiling = VK_IMAGE_TILING_OPTIMAL,
                                  .usage = to_vk(a.usage) | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                  .initialLayout = to_vk(ImageLayout::UNDEFINED) });
}

void ImageMetadataVk::destroy(Image& a, bool deallocate)
{
    if(!a.md.as_vk()) { return; }
    auto& backend = RendererBackendVk::get_instance();
    auto* md = a.md.as_vk();
    if(!md) { return; }
    if(deallocate && !md->is_aliased) { vmaDestroyImage(backend.vma, md->image, md->vmaa); }
    else if(deallocate && md->is_aliased) { vkDestroyImage(backend.dev, md->image, nullptr); }
    for(auto& [view, vkview] : a.md.as_vk()->views)
    {
        vkDestroyImageView(backend.dev, vkview.view, nullptr);
    }
    delete a.md.as_vk();
    a.md.ptr = nullptr;
}

void ImageViewMetadataVk::init(const ImageView& view, void** out_allocation)
{
    if(!out_allocation)
    {
        ENG_ERROR("Out allocation is nullptr");
        return;
    }

    if(!view.image)
    {
        ENG_ERROR("Invalid image");
        return;
    }

    auto& backend = RendererBackendVk::get_instance();
    auto& img = view.image.get();
    ENG_ASSERT(img.md.ptr != nullptr);

    const auto src_layer = view.src_subresource / img.mips;
    const auto src_mip = view.src_subresource % img.mips;
    const auto dst_layer = view.dst_subresource / img.mips;
    const auto dst_mip = view.dst_subresource % img.mips;
    const auto vkinfo =
        Vks(VkImageViewCreateInfo{ .image = view.image->md.as_vk()->image,
                                   .viewType = to_vk(view.type),
                                   .format = to_vk(view.format),
                                   .subresourceRange = { to_vk(get_aspect_from_format(view.format)), src_mip,
                                                         dst_mip - src_mip + 1, src_layer, dst_layer - src_layer + 1 } });

    auto* md = new ImageViewMetadataVk{};
    VK_CHECK(vkCreateImageView(backend.dev, &vkinfo, {}, &md->view));
    if(!md->view) { ENG_ERROR("Could not create image view for image {}", *view.image); }
    else { set_debug_name(md->view, ENG_FMT("image_{}_view", *view.image)); }
    *out_allocation = md;
}

void ImageViewMetadataVk::destroy(ImageView& a)
{
    if(!a) { return; }
    auto md = a.get_md();
    if(!md.vk) { return; }
    ENG_ASSERT(md.vk->view);
    auto& backend = RendererBackendVk::get_instance();
    vkDestroyImageView(backend.dev, md.vk->view, nullptr);
    delete md.vk;
    md.vk = nullptr;
}

void SamplerMetadataVk::init(Sampler& a)
{
    if(a.md.as_vk() != nullptr) { return; }
    auto vkinfo = Vks(VkSamplerCreateInfo{ .magFilter = gfx::to_vk(a.filtering.mag),
                                           .minFilter = gfx::to_vk(a.filtering.min),
                                           .mipmapMode = gfx::to_vk(a.mip_blending),
                                           .addressModeU = gfx::to_vk(a.addressing.u),
                                           .addressModeV = gfx::to_vk(a.addressing.v),
                                           .addressModeW = gfx::to_vk(a.addressing.w),
                                           .mipLodBias = a.lod.bias,
                                           .minLod = a.lod.min,
                                           .maxLod = a.lod.max });
    auto vkreduction = Vks(VkSamplerReductionModeCreateInfo{});
    if(a.reduction_mode != SamplerReductionMode::NONE)
    {
        vkreduction.reductionMode = gfx::to_vk(a.reduction_mode);
        vkinfo.pNext = &vkreduction;
    }
    auto* md = new SamplerMetadataVk{};
    a.md.ptr = md;
    VK_CHECK(vkCreateSampler(RendererBackendVk::get_dev(), &vkinfo, {}, &md->sampler));
}

void SamplerMetadataVk::destroy(Sampler& a)
{
    if(!a.md.as_vk()) { return; }
    vkDestroySampler(RendererBackendVk::get_dev(), a.md.as_vk()->sampler, nullptr);
    delete a.md.as_vk();
    a.md.ptr = nullptr;
}

void SwapchainMetadataVk::init(Swapchain& a)
{
    if(a.metadata)
    {
        ENG_ERROR("Swapchain is already initialized.");
        return;
    }
    Swapchain::acquire_impl = &acquire;
    a.images.resize(Renderer::frame_delay);
    a.views.resize(Renderer::frame_delay);
    auto* md = new SwapchainMetadataVk{};
    a.metadata = md;

    const auto image_usage_flags = ImageUsage::COLOR_ATTACHMENT_BIT | ImageUsage::TRANSFER_SRC_BIT | ImageUsage::TRANSFER_DST_BIT;
    const auto image_format = ImageFormat::R8G8B8A8_SRGB;
    auto& backend = RendererBackendVk::get_instance();
    const auto sinfo = Vks(VkSwapchainCreateInfoKHR{
        .surface = backend.window_surface,
        .minImageCount = Renderer::frame_delay,
        .imageFormat = to_vk(image_format),
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = VkExtent2D{ (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height },
        .imageArrayLayers = 1,
        .imageUsage = to_vk(image_usage_flags),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = true,
    });

    std::vector<VkImage> vkimgs(a.images.size());
    VK_CHECK(vkCreateSwapchainKHR(backend.dev, &sinfo, nullptr, &md->swapchain));
    VK_CHECK(vkGetSwapchainImagesKHR(backend.dev, md->swapchain, &Renderer::frame_delay, vkimgs.data()));

    auto& r = get_renderer();
    for(uint32_t i = 0; i < vkimgs.size(); ++i)
    {
        a.images[i] = r.make_image(ENG_FMT("swapchain_image_{}", i),
                                   Image::init(sinfo.imageExtent.width, sinfo.imageExtent.height, image_format, image_usage_flags),
                                   AllocateMemory::EXTERNAL, (void*)vkimgs[i]);
        a.views[i] = ImageView::init(a.images[i]);
    }
}

void SwapchainMetadataVk::destroy(Swapchain& a)
{
    if(!a.metadata) { return; }
    auto& backend = RendererBackendVk::get_instance();
    auto& md = get(a);
    vkDestroySwapchainKHR(backend.dev, md.swapchain, nullptr);
    ENG_ASSERT(a.images.size() == a.views.size());
    for(auto i = 0u; i < a.images.size(); ++i)
    {
        ImageMetadataVk::destroy(a.images.at(i).get(), false);
        Engine::get().renderer->images.erase(SlotIndex<uint32_t>{ *a.images.at(i) });
    }
    delete &md;
    a = Swapchain{};
}

SwapchainMetadataVk& SwapchainMetadataVk::get(Swapchain& a)
{
    ENG_ASSERT(a.metadata);
    return *(SwapchainMetadataVk*)a.metadata;
}

uint32_t SwapchainMetadataVk::acquire(Swapchain* a, uint64_t timeout, Sync* semaphore, Sync* fence)
{
    auto& backend = RendererBackendVk::get_instance();
    uint32_t index;
    VkSemaphore vksem{};
    VkFence vkfen{};
    if(semaphore) { vksem = semaphore->semaphore; }
    if(fence) { vkfen = fence->fence; }
    VK_CHECK(vkAcquireNextImageKHR(backend.dev, get(*a).swapchain, timeout, vksem, vkfen, &index));
    return index;
}

RendererBackendVk& RendererBackendVk::get_instance()
{
    return *static_cast<RendererBackendVk*>(Engine::get().renderer->backend);
}

void RendererBackendVk::init() { initialize_vulkan(); }

void RendererBackendVk::initialize_vulkan()
{
    if(volkInitialize() != VK_SUCCESS)
    {
        ENG_ERROR("Could not initialize volk");
        return;
    }

    vkb::InstanceBuilder builder;
    auto inst_ret = builder
                        .set_app_name("Example Vulkan Application")
#ifndef NDEBUG
                        .enable_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
                        .enable_validation_layers()
                        .use_default_debug_messenger()
#endif
                        .require_api_version(VK_MAKE_API_VERSION(0, 1, 3, 0))
                        .build();

    if(!inst_ret)
    {
        ENG_ERROR("Failed to create Vulkan instance. Error: {}", inst_ret.error().message());
        return;
    }

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
    if(!phys_ret)
    {
        ENG_ERROR("Failed to select Vulkan Physical Device.");
        return;
    }

    phys_ret.enable_extensions_if_present({
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
    });

    supports_raytracing = phys_ret.is_extension_present(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                          phys_ret.is_extension_present(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);

    auto synch2_features = Vks(VkPhysicalDeviceSynchronization2Features{ .synchronization2 = true });

    auto dyn_features = Vks(VkPhysicalDeviceDynamicRenderingFeatures{ .dynamicRendering = true });

    auto dev_2_features = Vks(VkPhysicalDeviceFeatures2{ .features = {
                                                             .geometryShader = true,
                                                             .multiDrawIndirect = true,
                                                             .fillModeNonSolid = true,
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
        device_builder.add_pNext(&acc_features).add_pNext(&rtpp_features).add_pNext(&maint5_features).add_pNext(&rayq_features);
    }
    auto dev_ret = device_builder.build();
    if(!dev_ret)
    {
        ENG_ERROR("Failed to create Vulkan device. Error: {}", dev_ret.error().message());
        return;
    }
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
    gq = new SubmitQueue{ dev, vkb_device.get_queue(vkb::QueueType::graphics).value(),
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
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT,
        .physicalDevice = pdev,
        .device = dev,
        .pVulkanFunctions = &vulkanFunctions,
        .instance = instance,
        .vulkanApiVersion = VK_API_VERSION_1_3,
    };
    VK_CHECK(vmaCreateAllocator(&allocatorCreateInfo, &vma));

    caps = RendererBackendCaps{
        .supports_bindless = true, // Should be really checked, but it's safe to assume true
    };
}

// void RendererBackendVulkan::initialize_resources()
//{
//     bindless_pool = new BindlessPool{ dev };
//     staging_buf->init(new SubmitQueue{ dev, gq->queue, gq->family_idx },
//                       [](Handle<Buffer> buffer) { RendererBackendVulkan::get_instance()->update_resource(buffer); });
//     // vertex_positions_buffer = make_buffer(BufferCreateInfo{
//     //     "vertex_positions_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
//     //                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT });
//     // vertex_attributes_buffer = make_buffer(BufferCreateInfo{
//     //     "vertex_attributes_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT });
//     // index_buffer =
//     //     make_buffer(BufferCreateInfo{ "index_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
//     // VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
//     //                                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT });
//     // meshlets_bs_buf = make_buffer(BufferCreateInfo{ "meshlets_bounding_spheres_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
//     // meshlets_mli_id_buf =
//     //     make_buffer(BufferCreateInfo{ "meshlets_meshlest_instance_to_id_buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
//
//     bufs.vpos_buf = make_buffer(BufferDescriptor{ "vertex positions", 1024, BufferUsage::STORAGE_BIT });
//     bufs.vattr_buf = make_buffer(BufferDescriptor{ "vertex attributes", 1024, BufferUsage::STORAGE_BIT });
//     bufs.idx_buf = make_buffer(BufferDescriptor{ "vertex indices", 1024, BufferUsage::STORAGE_BIT | BufferUsage::INDEX_BIT });
//     for(auto i = 0u; i < (uint32_t)MeshPassType::LAST_ENUM; ++i)
//     {
//         render_passes.at(i).cmd_buf = make_buffer(BufferDescriptor{ ENG_FMT("{}_cmds", to_string((MeshPassType)i)), 1024,
//                                                                     BufferUsage::STORAGE_BIT | BufferUsage::INDIRECT_BIT });
//         render_passes.at(i).ids_buf =
//             make_buffer(BufferDescriptor{ ENG_FMT("{}_ids", to_string((MeshPassType)i)), 1024, BufferUsage::STORAGE_BIT });
//     }
//     bufs.cull_bs_buf = make_buffer(BufferDescriptor{ "meshlets instance bbs", 1024, BufferUsage::STORAGE_BIT });
//     for(uint32_t i = 0; i < frame_datas.size(); ++i)
//     {
//         auto& fd = frame_datas[i];
//         bufs.trs_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("trs {}", i), 1024, BufferUsage::STORAGE_BIT });
//         bufs.const_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("constants_{}", i), 1024, BufferUsage::STORAGE_BIT });
//     }
//
//     // auto samp_ne = samplers.get_sampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
//     // auto samp_ll = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
//     // auto samp_lr = samplers.get_sampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
//
//     // ddgi.radiance_texture = make_image(Image{ "ddgi radiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
//     //                                           VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
//     //                                    ImageLayout::GENERAL);
//     // make_image(ddgi.radiance_texture, ImageLayout::GENERAL, samp_ll);
//
//     // ddgi.irradiance_texture = make_image(Image{ "ddgi irradiance", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
//     //                                             VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
//     //                                      ImageLayout::GENERAL);
//     // make_image(ddgi.irradiance_texture, ImageLayout::GENERAL, samp_ll);
//
//     // ddgi.visibility_texture = make_image(Image{ "ddgi visibility", 1, 1, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
//     //                                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
//     //                                      ImageLayout::GENERAL);
//     // make_image(ddgi.visibility_texture, ImageLayout::GENERAL, samp_ll);
//
//     // ddgi.probe_offsets_texture = make_image(Image{ "ddgi probe offsets", 1, 1, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
//     //                                                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT },
//     //                                         ImageLayout::GENERAL);
//     // make_image(ddgi.probe_offsets_texture, ImageLayout::GENERAL, samp_ll);
//
//     // vsm.constants_buffer = make_buffer(BufferCreateInfo{ "vms buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
//     // vsm.free_allocs_buffer = make_buffer(BufferCreateInfo{ "vms alloc buffer", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
//
//     // vsm.shadow_map_0 =
//     //     make_image("vsm image", VkImageCreateInfo{ .imageType = VK_IMAGE_TYPE_2D,
//     //                                                .format = VK_FORMAT_R32_SFLOAT,
//     //                                                .extent = { VSM_PHYSICAL_PAGE_RESOLUTION, VSM_PHYSICAL_PAGE_RESOLUTION, 1 },
//     //                                                .mipLevels = 1,
//     //                                                .arrayLayers = 1,
//     //                                                .samples = VK_SAMPLE_COUNT_1_BIT,
//     //                                                .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
//     //                                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT });
//
//     // vsm.dir_light_page_table =
//     //     make_image("vsm dir light 0 page table",
//     //                VkImageCreateInfo{
//     //                    .imageType = VK_IMAGE_TYPE_2D,
//     //                    .format = VK_FORMAT_R32_UINT,
//     //                    .extent = { VSM_VIRTUAL_PAGE_RESOLUTION, VSM_VIRTUAL_PAGE_RESOLUTION, 1 },
//     //                    .mipLevels = 1,
//     //                    .arrayLayers = VSM_NUM_CLIPMAPS,
//     //                    .samples = VK_SAMPLE_COUNT_1_BIT,
//     //                    .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
//     //                });
//
//     // vsm.dir_light_page_table_rgb8 =
//     //     make_image("vsm dir light 0 page table rgb8",
//     //                VkImageCreateInfo{
//     //                    .imageType = VK_IMAGE_TYPE_2D,
//     //                    .format = VK_FORMAT_R8G8B8A8_UNORM,
//     //                    .extent = { VSM_VIRTUAL_PAGE_RESOLUTION, VSM_VIRTUAL_PAGE_RESOLUTION, 1 },
//     //                    .mipLevels = 1,
//     //                    .arrayLayers = VSM_NUM_CLIPMAPS,
//     //                    .samples = VK_SAMPLE_COUNT_1_BIT,
//     //                    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
//     //                });
//
//     // create default material
// }

// void RendererBackendVulkan::initialize_mesh_passes()
//{
//     cull_pipeline = compile_pipeline(PipelineCreateInfo{
//         .shaders = { compile_shader(ShaderStage::COMPUTE_BIT, "culling/culling.comp.glsl") } });
//     hiz_pipeline = compile_pipeline(PipelineCreateInfo{
//         .shaders = { compile_shader(ShaderStage::COMPUTE_BIT, "culling/hiz.comp.glsl") } });
//     hiz_sampler = make_sampler(SamplerDescriptor{
//         .filtering = { ImageFilter::LINEAR, ImageFilter::LINEAR },
//         .addressing = { ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE, ImageAddressing::CLAMP_EDGE },
//         .mipmap_mode = SamplerMipmapMode::NEAREST,
//         .reduction_mode = SamplerReductionMode::MIN });
//
//     const auto pp_default_unlit = compile_pipeline(PipelineCreateInfo{
//         .shaders = { compile_shader(ShaderStage::VERTEX_BIT, "default_unlit/unlit.vert.glsl"),
//                      compile_shader(ShaderStage::PIXEL_BIT, "default_unlit/unlit.frag.glsl") },
//         .attachments = { .count = 1, .color_formats = { ImageFormat::R8G8B8A8_SRGB }, .depth_format =
//         ImageFormat::D32_SFLOAT }, .depth_test = true, .depth_write = true, .depth_compare = DepthCompare::GREATER,
//         .culling = CullFace::BACK,
//     });
//     MeshPassCreateInfo info{ .name = "default_unlit" };
//     info.effects[(uint32_t)MeshPassType::FORWARD] = make_shader_effect(ShaderEffect{ .pipeline = pp_default_unlit });
//     default_meshpass = make_mesh_pass(info);
//     default_material = materials.insert(Material{ .mesh_pass = default_meshpass }).handle;
// }

// void RendererBackendVulkan::create_window_sized_resources()
//{
//     // VkSwapchainMetadata::destroy(swapchain);
//     // VkSwapchainMetadata::init(swapchain);
//
//     for(auto i = 0ull; i < frame_datas.size(); ++i)
//     {
//         auto& fd = frame_datas.at(i);
//
//         // fd.hiz_pyramid = make_image(ImageDescriptor{
//         //     .name = ENG_FMT("hiz_pyramid_{}", i),
//         //     .width = (uint32_t)Engine::get().window->width,
//         //     .height = (uint32_t)Engine::get().window->height,
//         //     .mips = (uint32_t)std::log2f(std::max(Engine::get().window->width, Engine::get().window->height)) + 1,
//         //     .format = ImageFormat::D32_SFLOAT,
//         //     .usage = ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_SRC_BIT | ImageUsage::TRANSFER_DST_BIT });
//         // fd.hiz_debug_output =
//         //     make_image(ImageDescriptor{ .name = ENG_FMT("hiz_debug_output_{}", i),
//         //                                 .width = (uint32_t)Engine::get().window->width,
//         //                                 .height = (uint32_t)Engine::get().window->height,
//         //                                 .format = ImageFormat::R32FG32FB32FA32F,
//         //                                 .usage = ImageUsage::STORAGE_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_RW });
//
//         // fd.gbuffer.color_image = make_image(ImageDescriptor{ .name = ENG_FMT("g_color_{}", i),
//         //                                                      .width = (uint32_t)Engine::get().window->width,
//         //                                                      .height = (uint32_t)Engine::get().window->height,
//         //                                                      .format = ImageFormat::R8G8B8A8_SRGB,
//         //                                                      .usage = ImageUsage::COLOR_ATTACHMENT_BIT |
//         //                                                               ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_RW });
//         // fd.gbuffer.depth_buffer_image = make_image(ImageDescriptor{
//         //     .name = ENG_FMT("g_depth_{}", i),
//         //     .width = (uint32_t)Engine::get().window->width,
//         //     .height = (uint32_t)Engine::get().window->height,
//         //     .format = ImageFormat::D32_SFLOAT,
//         //     .usage = ImageUsage::DEPTH_STENCIL_ATTACHMENT_BIT | ImageUsage::SAMPLED_BIT | ImageUsage::TRANSFER_RW });
//
//         /*make_image(&fd.gbuffer.color_image,
//                      Image{ ENG_FMT("g_color_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
//                             VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLE_COUNT_1_BIT,
//                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
//           make_image(&fd.gbuffer.view_space_positions_image,
//                      Image{ ENG_FMT("g_view_pos_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
//                             VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
//                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
//                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });
//           make_image(&fd.gbuffer.view_space_normals_image,
//                      Image{ ENG_FMT("g_view_nor_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
//                             VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
//                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
//           make_image(&fd.gbuffer.depth_buffer_image,
//                      Image{ ENG_FMT("g_depth_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
//                             VK_FORMAT_D16_UNORM, VK_SAMPLE_COUNT_1_BIT,
//                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT });
//           make_image(&fd.gbuffer.ambient_occlusion_image,
//                      Image{ ENG_FMT("ao_{}", i), (uint32_t)screen_rect.w, (uint32_t)screen_rect.h, 1, 1, 1,
//                             VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
//                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT });*/
//     }
//
//     // auto cmd = frame_datas[0].cmdpool->begin();
//     // for(auto i = 0ull; i < frame_datas.size(); ++i)
//     //{
//     //     cmd->barrier(frame_datas[i].hiz_pyramid.get(), PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::ALL,
//     //                  PipelineAccess::NONE, ImageLayout::UNDEFINED, ImageLayout::GENERAL);
//     //     cmd->barrier(frame_datas[i].hiz_debug_output.get(), PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT,
//     //                  PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW, ImageLayout::UNDEFINED, ImageLayout::GENERAL);
//     //     auto& img = frame_datas[i].gbuffer.depth_buffer_image.get();
//     //     cmd->clear_depth_stencil(img, 0.0f, 0);
//     //     cmd->barrier(img, PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::EARLY_Z_BIT,
//     //                  PipelineAccess::DS_RW, ImageLayout::TRANSFER_DST, ImageLayout::READ_ONLY);
//     //     bufs.trs_bufs[i] = make_buffer(BufferDescriptor{ ENG_FMT("transform_buffer_{}", i), 1024, BufferUsage::STORAGE_BIT });
//     // }
//     // frame_datas[0].cmdpool->end(cmd);
//     // submit_queue->with_cmd_buf(cmd).submit_wait(~0ull);
// }

// void RendererBackendVulkan::build_render_graph()
//{
//     ENG_TODO();
//     // rendergraph.clear_passes();
//     //  rendergraph.add_pass<FFTOceanDebugGenH0Pass>(&rendergraph);
//     //  rendergraph.add_pass<FFTOceanDebugGenHtPass>(&rendergraph);
//     //  rendergraph.add_pass<FFTOceanDebugGenFourierPass>(&rendergraph);
//     //  rendergraph.add_pass<FFTOceanDebugGenDisplacementPass>(&rendergraph);
//     //  rendergraph.add_pass<FFTOceanDebugGenGradientPass>(&rendergraph);
//     // rendergraph.add_pass<ZPrepassPass>(&rendergraph);
//     //  rendergraph.add_pass<VsmClearPagesPass>(&rendergraph);
//     //  rendergraph.add_pass<VsmPageAllocPass>(&rendergraph);
//     //  rendergraph.add_pass<VsmShadowsPass>(&rendergraph);
//     //  rendergraph.add_pass<VsmDebugPageCopyPass>(&rendergraph);
//     // rendergraph.add_pass<DefaultUnlitPass>(&rendergraph);
//     // rendergraph.add_pass<ImguiPass>(&rendergraph);
//     // rendergraph.add_pass<SwapchainPresentPass>(&rendergraph);
//     // rendergraph.bake();
// }

// void RendererBackendVulkan::update()
//{
//     // if(flags.test(RenderFlags::PAUSE_RENDERING)) { return; }
//     // if(flags.test_clear(RenderFlags::DIRTY_GEOMETRY_BATCHES_BIT))
//     //{
//     //     // ENG_ASSERT(false);
//     //     //  upload_staged_models();
//     // }
//     // if(flags.test_clear(RenderFlags::DIRTY_MESH_INSTANCES))
//     //{
//     //     // ENG_ASSERT(false);
//     //     //  upload_transforms();
//     // }
//     // if(flags.test_clear(RenderFlags::DIRTY_BLAS_BIT)) { build_blas(); }
//     // if(flags.test_clear(RenderFlags::DIRTY_TLAS_BIT))
//     //{
//     //     build_tlas();
//     //     update_ddgi();
//     //     // TODO: prepare ddgi on scene update
//     // }
//     // if(flags.test_clear(RenderFlags::RESIZE_SWAPCHAIN_BIT))
//     //{
//     //     submit_queue->wait_idle();
//     //     create_window_sized_resources();
//     // }
//     // if(flags.test_clear(RenderFlags::REBUILD_RENDER_GRAPH))
//     //{
//     //     ENG_ASSERT(false);
//     //     // build_render_graph();
//     //     // pipelines.threaded_compile();
//     // }
//     // if(flags.test_clear(RenderFlags::UPDATE_BINDLESS_SET))
//     //{
//     //     ENG_ASSERT(false);
//     //     submit_queue->wait_idle();
//     //     // update_bindless_set();
//     // }
//     if(!shaders_to_compile.empty()) { compile_shaders(); }
//     if(!pipelines_to_compile.empty()) { compile_pipelines(); }
//
//     auto& fd = get_frame_data();
//     const auto frame_num = Engine::get().frame_num;
//     // fd.rendering_fence->wait_cpu(~0ull);
//     // fd.cmdpool->reset();
//
//     // uint32_t swapchain_index{};
//     // Image* swapchain_image{};
//     //{
//     //     VkResult acquire_ret;
//     //     swapchain_index = swapchain.acquire(~0ull, fd.acquire_semaphore);
//     //     swapchain_image = &swapchain.get_image().get();
//     // }
//
//     // fd.rendering_fence->reset();
//
// #if 0
//     static glm::mat4 s_view = Engine::get().camera->prev_view;
//     if(true || (glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
//     {
//         s_view = Engine::get().camera->prev_view;
//     }
// #endif
//
//     {
//         const float hx = (halton(Engine::get().frame_num % 4u, 2) * 2.0 - 1.0);
//         const float hy = (halton(Engine::get().frame_num % 4u, 3) * 2.0 - 1.0);
//         const glm::mat3 rand_mat =
//             glm::mat3_cast(glm::angleAxis(hy, glm::vec3{ 1.0, 0.0, 0.0 }) * glm::angleAxis(hx, glm::vec3{ 0.0, 1.0, 0.0 }));
//
//         std::swap(bufs.const_bufs[0], bufs.const_bufs[1]);
//
//         const auto view = Engine::get().camera->get_view();
//         const auto proj = Engine::get().camera->get_projection();
//         const auto invview = glm::inverse(view);
//         const auto invproj = glm::inverse(proj);
//
//         GPUEngConstantsBuffer cb{
//             .vposb = get_bindless(bufs.vpos_buf),
//             .vatrb = get_bindless(bufs.vattr_buf),
//             .vidxb = get_bindless(bufs.idx_buf),
//             .rmbsb = get_bindless(bufs.cull_bs_buf),
//             .view = view,
//             .proj = proj,
//             .proj_view = proj * view,
//             .inv_view = invview,
//             .inv_proj = invproj,
//             .inv_proj_view = invview * invproj,
//             .rand_mat = rand_mat,
//             .cam_pos = Engine::get().camera->pos,
//         };
//         staging_buf->copy(bufs.const_bufs[0], &cb, 0ull, { 0, sizeof(cb) });
//         // const auto ldir = glm::normalize(*(glm::vec3*)Engine::get().scene->debug_dir_light_dir);
//         // const auto cam = Engine::get().camera->pos;
//         // auto eye = -ldir * 30.0f;
//         // const auto lview = glm::lookAt(eye, eye + ldir, glm::vec3{ 0.0f, 1.0f, 0.0f });
//         // const auto eyelpos = lview * glm::vec4{ cam, 1.0f };
//         // const auto offset = glm::translate(glm::mat4{ 1.0f }, -glm::vec3{ eyelpos.x, eyelpos.y, 0.0f });
//         // const auto dir_light_view = offset * lview;
//         // const auto eyelpos2 = dir_light_view * glm::vec4{ cam, 1.0f };
//         // ENG_LOG("CAMERA EYE DOT {} {}", eyelpos2.x, eyelpos2.y);
//         //// const auto dir_light_proj = glm::perspectiveFov(glm::radians(90.0f), 8.0f * 1024.0f, 8.0f * 1024.0f, 0.0f, 150.0f);
//
//         // GPUVsmConstantsBuffer vsmconsts{
//         //     .dir_light_view = dir_light_view,
//         //     .dir_light_dir = ldir,
//         //     .num_pages_xy = VSM_VIRTUAL_PAGE_RESOLUTION,
//         //     .max_clipmap_index = 0,
//         //     .texel_resolution = float(VSM_PHYSICAL_PAGE_RESOLUTION),
//         //     .num_frags = 0,
//         // };
//
//         // for(int i = 0; i < VSM_NUM_CLIPMAPS; ++i)
//         //{
//         //     float halfSize = float(VSM_CLIP0_LENGTH) * 0.5f * std::exp2f(float(i));
//         //     float splitNear = (i == 0) ? 0.1f : float(VSM_CLIP0_LENGTH) * std::exp2f(float(i - 1));
//         //     float splitFar = float(VSM_CLIP0_LENGTH) * std::exp2f(float(i));
//         //     splitNear = 1.0;
//         //     splitFar = 75.0;
//         //     vsmconsts.dir_light_proj_view[i] =
//         //         glm::ortho(-halfSize, +halfSize, -halfSize, +halfSize, splitNear, splitFar) * dir_light_view;
//         // }
//
//         // GPUConstantsBuffer constants{
//         //     .debug_view = s_view,
//         //     .view = Engine::get().camera->get_view(),
//         //     .proj = Engine::get().camera->get_projection(),
//         //     .proj_view = Engine::get().camera->get_projection() * Engine::get().camera->get_view(),
//         //     .inv_view = glm::inverse(Engine::get().camera->get_view()),
//         //     .inv_proj = glm::inverse(Engine::get().camera->get_projection()),
//         //     .inv_proj_view = glm::inverse(Engine::get().camera->get_projection() * Engine::get().camera->get_view()),
//         //     .cam_pos = Engine::get().camera->pos,
//         // };
//         // staging_buf->copy(fd.constants, &constants, 0, { 0, sizeof(constants) });
//         // staging_buffer->stage(vsm.constants_buffer, vsmconsts, 0ull);
//     }
//
//     if(flags.test_clear(RenderFlags::DIRTY_TRANSFORMS_BIT)) { build_transforms_buffer(); }
//
//     // uint32_t old_triangles = *((uint32_t*)bufs.buf_draw_cmds->memory + 1);
//     bake_indirect_commands();
//     staging_buf->flush();
//     // const auto cmd = fd.cmdpool->begin();
//     //  rendergraph.render(cmd);
//
// #if 0
//     struct PushConstantsCulling
//     {
//         uint32_t constants_index;
//         uint32_t ids_index;
//         uint32_t post_cull_ids_index;
//         uint32_t bs_index;
//         uint32_t transforms_index;
//         uint32_t indirect_commands_index;
//         uint32_t hiz_source;
//         uint32_t hiz_dest;
//         uint32_t hiz_width;
//         uint32_t hiz_height;
//     };
//
//     PushConstantsCulling push_constants_culling{ .constants_index = bindless_pool->get_index(fd.constants),
//                                                  .ids_index = bindless_pool->get_index(bufs.buf_draw_ids),
//                                                  .post_cull_ids_index = bindless_pool->get_index(bufs.buf_final_draw_ids),
//                                                  .bs_index = bindless_pool->get_index(bufs.cull_bs_buf),
//                                                  .transforms_index = bindless_pool->get_index(bufs.trs_bufs[0]),
//                                                  .indirect_commands_index = bindless_pool->get_index(bufs.buf_draw_cmds) };
//
//     {
//         auto& dep_image = fd.gbuffer.depth_buffer_image.get();
//         auto& hiz_image = fd.hiz_pyramid.get();
//         cmd->bind_pipeline(hiz_pipeline.get());
//         bindless_pool->bind(cmd);
//         if(true || (glfwGetKey(Engine::get().window->window, GLFW_KEY_0) == GLFW_PRESS))
//         {
//             cmd->clear_depth_stencil(hiz_image, 0.0f, 0);
//             cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
//                          PipelineAccess::SHADER_RW);
//             cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE,
//                          PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::READ_ONLY);
//
//             push_constants_culling.hiz_width = hiz_image.width;
//             push_constants_culling.hiz_height = hiz_image.height;
//
//             for(auto i = 0u; i < hiz_image.mips; ++i)
//             {
//                 if(i == 0)
//                 {
//                     push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
//                         make_view(ImageViewDescriptor{ .image = fd.gbuffer.depth_buffer_image, .aspect = ImageAspect::DEPTH }),
//                         hiz_sampler, ImageLayout::READ_ONLY }));
//                 }
//                 else
//                 {
//                     push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
//                         make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { i - 1, 1 } }),
//                         hiz_sampler, ImageLayout::GENERAL }));
//                 }
//                 push_constants_culling.hiz_dest = bindless_pool->get_index(make_texture(TextureDescriptor{
//                     make_view(ImageViewDescriptor{ .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { i, 1 } }),
//                     {},
//                     ImageLayout::GENERAL }));
//                 push_constants_culling.hiz_width = std::max(hiz_image.width >> i, 1u);
//                 push_constants_culling.hiz_height = std::max(hiz_image.height >> i, 1u);
//                 cmd->push_constants(ShaderStage::ALL, &push_constants_culling, { 0, sizeof(push_constants_culling) });
//                 cmd->dispatch((push_constants_culling.hiz_width + 31) / 32, (push_constants_culling.hiz_height + 31) / 32, 1);
//                 cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
//                              PipelineAccess::SHADER_RW);
//             }
//         }
//         else
//         {
//             cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE,
//                          PipelineStage::ALL, PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::READ_ONLY);
//         }
//         push_constants_culling.hiz_source = bindless_pool->get_index(make_texture(TextureDescriptor{
//             make_view(ImageViewDescriptor{
//                 .image = fd.hiz_pyramid, .aspect = ImageAspect::DEPTH, .mips = { 0u, hiz_image.mips } }),
//             hiz_sampler, ImageLayout::GENERAL }));
//         push_constants_culling.hiz_dest = bindless_pool->get_index(make_texture(TextureDescriptor{
//             make_view(ImageViewDescriptor{ .image = fd.hiz_debug_output, .aspect = ImageAspect::COLOR }), {}, ImageLayout::GENERAL }));
//         cmd->clear_color(fd.hiz_debug_output.get(), ImageLayout::GENERAL, { 0, 1 }, { 0, 1 }, 0.0f);
//         cmd->barrier(PipelineStage::TRANSFER_BIT, PipelineAccess::TRANSFER_WRITE_BIT, PipelineStage::COMPUTE_BIT,
//                      PipelineAccess::SHADER_RW);
//         cmd->bind_pipeline(cull_pipeline.get());
//         cmd->push_constants(ShaderStage::ALL, &push_constants_culling, { 0, sizeof(push_constants_culling) });
//         cmd->dispatch((meshlet_instances.size() + 63) / 64, 1, 1);
//         cmd->barrier(PipelineStage::COMPUTE_BIT, PipelineAccess::SHADER_WRITE_BIT, PipelineStage::INDIRECT_BIT,
//                      PipelineAccess::INDIRECT_READ_BIT);
//     }
//
//     VkRenderingAttachmentInfo rainfos[]{
//         Vks(VkRenderingAttachmentInfo{ .imageView = VkImageViewMetadata::get(swapchain_image->default_view.get()).view,
//                                        .imageLayout = to_vk(ImageLayout::ATTACHMENT),
//                                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
//                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
//                                        .clearValue = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } } }),
//         Vks(VkRenderingAttachmentInfo{ .imageView =
//                                            VkImageViewMetadata::get(fd.gbuffer.depth_buffer_image->default_view.get()).view,
//                                        .imageLayout = to_vk(ImageLayout::ATTACHMENT),
//                                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
//                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
//                                        .clearValue = { .depthStencil = { .depth = 0.0f, .stencil = 0 } } })
//     };
//     const auto rinfo = Vks(VkRenderingInfo{ .renderArea = { { 0, 0 }, { swapchain_image->width, swapchain_image->height } },
//                                             .layerCount = 1,
//                                             .colorAttachmentCount = 1,
//                                             .pColorAttachments = rainfos,
//                                             .pDepthAttachment = &rainfos[1] });
//     struct push_constants_1
//     {
//         uint32_t indices_index;
//         uint32_t vertex_positions_index;
//         uint32_t vertex_attributes_index;
//         uint32_t transforms_index;
//         uint32_t constants_index;
//         uint32_t meshlet_instance_index;
//         uint32_t meshlet_ids_index;
//         uint32_t meshlet_bs_index;
//         uint32_t hiz_pyramid_index;
//         uint32_t hiz_debug_index;
//     };
//     push_constants_1 pc1{
//         .indices_index = bindless_pool->get_index(bufs.idx_buf),
//         .vertex_positions_index = bindless_pool->get_index(bufs.vpos_buf),
//         .vertex_attributes_index = bindless_pool->get_index(bufs.vattr_buf),
//         .transforms_index = bindless_pool->get_index(bufs.trs_bufs[0]),
//         .constants_index = bindless_pool->get_index(fd.constants),
//         .meshlet_instance_index = bindless_pool->get_index(bufs.buf_draw_ids),
//         .meshlet_ids_index = bindless_pool->get_index(bufs.buf_final_draw_ids),
//         .meshlet_bs_index = bindless_pool->get_index(bufs.cull_bs_buf),
//         .hiz_pyramid_index = push_constants_culling.hiz_source,
//         .hiz_debug_index = bindless_pool->get_index(make_texture(TextureDescriptor{
//             make_view(ImageViewDescriptor{ .image = fd.hiz_debug_output
//
//             }),
//             make_sampler(SamplerDescriptor{ .mip_lod = { 0.0f, 1.0f, 0.0 } }), ImageLayout::READ_ONLY })),
//     };
//
//     cmd->bind_index(bufs.idx_buf.get(), 0, VK_INDEX_TYPE_UINT16);
//     cmd->barrier(*swapchain_image, PipelineStage::NONE, PipelineAccess::NONE, PipelineStage::COLOR_OUT_BIT,
//                  PipelineAccess::COLOR_WRITE_BIT, ImageLayout::UNDEFINED, ImageLayout::ATTACHMENT);
//     cmd->barrier(fd.gbuffer.depth_buffer_image.get(), PipelineStage::ALL, PipelineAccess::NONE,
//                  PipelineStage::EARLY_Z_BIT, PipelineAccess::DS_RW, ImageLayout::UNDEFINED, ImageLayout::ATTACHMENT);
//     cmd->begin_rendering(rinfo);
//
//     VkViewport viewport{ 0.0f, 0.0f, Engine::get().window->width, Engine::get().window->height, 0.0f, 1.0f };
//     VkRect2D scissor{ {}, { (uint32_t)Engine::get().window->width, (uint32_t)Engine::get().window->height } };
//     for(auto i = 0u, off = 0u; i < multibatches.size(); ++i)
//     {
//         const auto& mb = multibatches.at(i);
//         const auto& p = pipelines.at(mb.pipeline);
//         cmd->bind_pipeline(p);
//         if(i == 0) { bindless_pool->bind(cmd); }
//         cmd->push_constants(ShaderStage::ALL, &pc1, { 0u, sizeof(pc1) });
//         cmd->set_viewports(&viewport, 1);
//         cmd->set_scissors(&scissor, 1);
//         cmd->draw_indexed_indirect_count(bufs.buf_draw_cmds.get(), 8, bufs.buf_draw_cmds.get(), 0, bufs.command_count,
//                                          sizeof(DrawIndexedIndirectCommand));
//     }
//     cmd->end_rendering();
//
//     Engine::get().imgui_renderer->render(cmd);
//
//     cmd->barrier(*swapchain_image, PipelineStage::COLOR_OUT_BIT, PipelineAccess::COLOR_WRITE_BIT, PipelineStage::ALL,
//                  PipelineAccess::NONE, ImageLayout::ATTACHMENT, ImageLayout::PRESENT);
//
//     fd.cmdpool->end(cmd);
//     submit_queue->with_cmd_buf(cmd)
//         .wait_sync(staging_buf->flush(), PipelineStage::ALL)
//         .wait_sync(fd.acquire_semaphore, PipelineStage::COLOR_OUT_BIT)
//         .signal_sync(fd.rendering_semaphore, PipelineStage::ALL)
//         .signal_sync(fd.rendering_fence)
//         .submit();
//
//     submit_queue->wait_sync(fd.rendering_semaphore, PipelineStage::ALL).present(&swapchain);
//     if(!flags.empty()) { ENG_WARN("render flags not empty at the end of the frame: {:b}", flags.flags); }
//
//     flags.clear();
//     submit_queue->wait_idle();
//
//     uint32_t new_triangles = *((uint32_t*)bufs.buf_draw_cmds->memory + 2);
//     ENG_LOG("NUM TRIANGLES (PRE | POST) {} | {}; DIFF: {}", old_triangles, new_triangles, new_triangles - old_triangles);
//     return;
// #endif
// }

void RendererBackendVk::allocate_buffer(Buffer& buffer, AllocateMemory allocate)
{
    BufferMetadataVk::init(buffer, allocate);
}

void RendererBackendVk::destroy_buffer(Buffer& buffer) { BufferMetadataVk::destroy(buffer); }

void RendererBackendVk::allocate_image(Image& image, AllocateMemory allocate, void* user_data)
{
    ImageMetadataVk::init(image, allocate, user_data);
}

void RendererBackendVk::destroy_image(Image& image) { ImageMetadataVk::destroy(image); }

void RendererBackendVk::allocate_view(const ImageView& view, void** out_allocation)
{
    ImageViewMetadataVk::init(view, out_allocation);
}

void RendererBackendVk::allocate_sampler(Sampler& sampler) { SamplerMetadataVk::init(sampler); }

void RendererBackendVk::make_shader(Shader& shader) { shader.md.vk = new ShaderMetadataVk{}; }

bool RendererBackendVk::compile_shader(const Shader& shader)
{
    auto* shmd = shader.md.vk;
    ENG_ASSERT(shmd);
    static const auto read_file = [](const std::filesystem::path& file_path) {
        std::string file_path_str = file_path.string();
        std::string include_paths = eng::paths::SHADERS_DIR.string();
        char stbi_error[256] = {};
        char* stb_include_cstr = stb_include_file(file_path_str.data(), nullptr, include_paths.data(), stbi_error);
        if(!stb_include_cstr)
        {
            ENG_WARN("STBI_INCLUDE cannot parse file [{}]: {}", file_path_str, stbi_error);
            return std::string{};
        }
        std::string stb_include_str{ stb_include_cstr };
        free(stb_include_cstr);
        return stb_include_str;
    };

    const auto shckind = [stage = shader.stage] {
        if(stage == ShaderStage::VERTEX_BIT) { return shaderc_vertex_shader; }
        if(stage == ShaderStage::PIXEL_BIT) { return shaderc_fragment_shader; }
        // if(stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) { return shaderc_raygen_shader; }
        // if(stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) { return shaderc_closesthit_shader; }
        // if(stage == VK_SHADER_STAGE_MISS_BIT_KHR) { return shaderc_miss_shader; }
        if(stage == ShaderStage::COMPUTE_BIT) { return shaderc_compute_shader; }
        ENG_ERROR("Unrecognized shader type");
        return shaderc_vertex_shader;
    }();

    shaderc::CompileOptions shcopts;
    shcopts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    shcopts.SetTargetSpirv(shaderc_spirv_version_1_6);
    shcopts.SetGenerateDebugInfo();
    // shcopts.AddMacroDefinition("ASDF");

    std::string shader_str = read_file(shader.path);
    const auto shader_str_hash = eng::hash::combine_fnv1a(shader_str);

    std::vector<uint32_t> out_spv;
    const auto pc_spv_path = std::filesystem::path{ shader.path.string() + ".precompiled" };
    std::fstream pc_spv_file{ pc_spv_path, std::fstream::binary | std::fstream::ate | std::fstream::in };
    if(pc_spv_file.is_open())
    {
        const size_t pc_spv_file_size = pc_spv_file.tellg();
        pc_spv_file.seekg(std::ios::beg);
        ENG_ASSERT(pc_spv_file_size > 0);
        char pc_spv_hash_arr[8];
        pc_spv_file.read(pc_spv_hash_arr, 8);
        const auto pc_spv_hash = std::bit_cast<uint64_t>(pc_spv_hash_arr);
        if(pc_spv_hash == shader_str_hash)
        {
            out_spv.resize((pc_spv_file_size - sizeof(pc_spv_hash)) / sizeof(out_spv[0]));
            pc_spv_file.read(reinterpret_cast<char*>(out_spv.data()), pc_spv_file_size - 8);
        }
        pc_spv_file.close();
    }

    if(out_spv.empty())
    {
        ENG_LOG("Compiling shader {}", shader.path.string());
        shaderc::Compiler shccomp;
        const auto res = shccomp.CompileGlslToSpv(shader_str, shckind, shader.path.filename().string().c_str(), shcopts);
        if(res.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            ENG_WARN("Could not compile shader : {}, because : \"{}\"", shader.path.string(), res.GetErrorMessage());
            return false;
        }
        out_spv = { res.begin(), res.end() };
        pc_spv_file.open(pc_spv_path, std::fstream::out | std::fstream::binary);
        char pc_spv_hash_arr[8];
        memcpy(pc_spv_hash_arr, &shader_str_hash, 8);
        pc_spv_file.write(pc_spv_hash_arr, 8);
        pc_spv_file.write(reinterpret_cast<const char*>(out_spv.data()), out_spv.size() * sizeof(out_spv[0]));
    }

    pc_spv_file.close();

    const auto module_info = Vks(VkShaderModuleCreateInfo{
        .codeSize = out_spv.size() * sizeof(uint32_t),
        .pCode = out_spv.data(),
    });

    if(shmd->shader) { vkDestroyShaderModule(dev, shmd->shader, nullptr); }
    VK_CHECK(vkCreateShaderModule(dev, &module_info, nullptr, &shmd->shader));

    return true;
}

bool RendererBackendVk::compile_layout(DescriptorLayout& layout)
{
    DescriptorLayoutMetadataVk::init(layout);
    return true;
}

bool RendererBackendVk::compile_layout(PipelineLayout& layout)
{
    PipelineLayoutMetadataVk::init(layout);
    return true;
}

void RendererBackendVk::make_pipeline(Pipeline& pipeline)
{
    const auto stage = pipeline.info.shaders[0]->stage;
    if(stage == ShaderStage::VERTEX_BIT) { pipeline.type = PipelineType::GRAPHICS; }
    else if(stage == ShaderStage::COMPUTE_BIT) { pipeline.type = PipelineType::COMPUTE; }
    else
    {
        ENG_ERROR("Unrecognized pipeline type");
        return;
    }
    pipeline.md.vk = new PipelineMetadataVk{};
}

bool RendererBackendVk::compile_pipeline(const Pipeline& pipeline)
{
    PipelineMetadataVk::init(pipeline);
    return true;
}

Sync* RendererBackendVk::make_sync(const SyncCreateInfo& info)
{
    auto* s = new Sync{};
    s->init(info);
    return s;
}

void RendererBackendVk::destory_sync(Sync* sync)
{
    ENG_ASSERT(sync);
    sync->destroy();
    delete sync;
}

Swapchain* RendererBackendVk::make_swapchain()
{
    auto* sw = new Swapchain{};
    SwapchainMetadataVk::init(*sw);
    return sw;
}

SubmitQueue* RendererBackendVk::get_queue(QueueType type)
{
    if(type == QueueType::GRAPHICS) { return gq; }
    ENG_ERROR("Unsupported queue");
    return nullptr;
}

ImageView::Metadata RendererBackendVk::get_md(const ImageView& view)
{
    auto& img = view.image.get();
    auto it = img.md.as_vk()->views.find(view);
    ImageViewMetadataVk* retmd{};
    if(it == img.md.as_vk()->views.end())
    {
        ImageViewMetadataVk* md{};
        get_renderer().backend->allocate_view(view, (void**)&md);
        retmd = &img.md.as_vk()->views.emplace(view, *md).first->second;
        delete md; // delete md here, as allocate_view currently allocates dynamic memory and we do a copy to views map
    }
    else { retmd = &it->second; }
    return ImageView::Metadata{ .vk = retmd };
}

size_t RendererBackendVk::get_indirect_indexed_command_size() const { return sizeof(IndirectIndexedCommand); }

void RendererBackendVk::make_indirect_indexed_command(void* out, uint32_t index_count, uint32_t instance_count,
                                                      uint32_t first_index, int32_t first_vertex, uint32_t first_instance) const
{
    ENG_ASSERT(out != nullptr);
    IndirectIndexedCommand cmd{ index_count, instance_count, first_index, first_vertex, first_instance };
    memcpy(out, &cmd, sizeof(cmd));
}

void RendererBackendVk::get_memory_requirements(const Buffer& resource, RendererMemoryRequirements& reqs)
{
    VkBufferMemoryRequirementsInfo2 res_reqs{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
                                              .buffer = resource.md.as_vk()->buffer };
    VkMemoryRequirements2 mem_reqs{ .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    vkGetBufferMemoryRequirements2(dev, &res_reqs, &mem_reqs);
    if(reqs.size == 0)
    {
        reqs.size = mem_reqs.memoryRequirements.size;
        reqs.alignment = mem_reqs.memoryRequirements.alignment;
        reqs.backend_data[0] = mem_reqs.memoryRequirements.memoryTypeBits;
    }
    else
    {
        reqs.size = std::max(reqs.size, mem_reqs.memoryRequirements.size);
        reqs.alignment = std::max(reqs.alignment, mem_reqs.memoryRequirements.alignment);
        reqs.backend_data[0] &= mem_reqs.memoryRequirements.memoryTypeBits;
    }
}

void RendererBackendVk::get_memory_requirements(const Image& resource, RendererMemoryRequirements& reqs)
{
    VkImageMemoryRequirementsInfo2 res_reqs{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                                             .image = resource.md.as_vk()->image };
    VkMemoryRequirements2 mem_reqs{ .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    vkGetImageMemoryRequirements2(dev, &res_reqs, &mem_reqs);
    if(reqs.size == 0)
    {
        reqs.size = mem_reqs.memoryRequirements.size;
        reqs.alignment = mem_reqs.memoryRequirements.alignment;
        reqs.backend_data[0] = mem_reqs.memoryRequirements.memoryTypeBits;
    }
    else
    {
        reqs.size = std::max(reqs.size, mem_reqs.memoryRequirements.size);
        reqs.alignment = std::max(reqs.alignment, mem_reqs.memoryRequirements.alignment);
        reqs.backend_data[0] &= mem_reqs.memoryRequirements.memoryTypeBits;
    }
}

void* RendererBackendVk::allocate_aliasable_memory(const RendererMemoryRequirements& reqs)
{
    const VkMemoryRequirements vkreqs{ .size = reqs.size, .alignment = reqs.alignment, .memoryTypeBits = reqs.backend_data[0] };
    const VmaAllocationCreateInfo info{ .preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT };
    VmaAllocation alloc;
    VK_CHECK(vmaAllocateMemory(vma, &vkreqs, &info, &alloc, nullptr));
    return alloc;
}

void RendererBackendVk::bind_aliasable_memory(Buffer& resource, void* memory, size_t offset)
{
    auto alloc = (VmaAllocation)memory;
    RendererMemoryRequirements reqs{};
    get_memory_requirements(resource, reqs);
    if(resource.md.as_vk() && resource.md.as_vk()->vma_alloc && !resource.md.as_vk()->is_aliased)
    {
        ENG_ERROR("Resource already has dedicated memory");
        return;
    }
    if(!resource.md.as_vk()) { resource.md.ptr = new BufferMetadataVk{}; }
    ENG_ASSERT(resource.md.as_vk());
    if(!resource.md.as_vk()->buffer)
    {
        VkBufferCreateInfo vkinfo;
        BufferMetadataVk::init(resource, vkinfo);
        VK_CHECK(vkCreateBuffer(dev, &vkinfo, nullptr, &resource.md.as_vk()->buffer));
    }
    if(!resource.md.as_vk()->buffer)
    {
        ENG_ERROR("Buffer is null");
        return;
    }
    VK_CHECK(vmaBindBufferMemory2(vma, alloc, offset, resource.md.as_vk()->buffer, nullptr));
    resource.md.as_vk()->is_aliased = true;
    resource.md.as_vk()->vma_alloc = alloc;
}

void RendererBackendVk::bind_aliasable_memory(Image& resource, void* memory, size_t offset)
{
    auto alloc = (VmaAllocation)memory;
    RendererMemoryRequirements reqs{};
    get_memory_requirements(resource, reqs);
    if(!resource.md.ptr) { resource.md.ptr = new ImageMetadataVk{}; }
    ENG_ASSERT(resource.md.ptr);
    if(!resource.md.as_vk()->image)
    {
        VkImageCreateInfo vkinfo;
        ImageMetadataVk::init(resource, vkinfo);
        VK_CHECK(vkCreateImage(dev, &vkinfo, nullptr, &resource.md.as_vk()->image));
    }
    if(!resource.md.as_vk()->image)
    {
        ENG_ERROR("Image is null");
        return;
    }
    VK_CHECK(vmaBindImageMemory2(vma, alloc, offset, resource.md.as_vk()->image, nullptr));
    resource.md.as_vk()->is_aliased = true;
    resource.md.as_vk()->vmaa = alloc;
}

void RendererBackendVk::set_debug_name(Buffer& resource, std::string_view name) const
{
    eng::gfx::set_debug_name(resource.md.as_vk()->buffer, name);
}

void RendererBackendVk::set_debug_name(Image& resource, std::string_view name) const
{
    eng::gfx::set_debug_name(resource.md.as_vk()->image, name);
}

// todo: swapchain impl should not be here
uint32_t Swapchain::acquire(uint64_t timeout, Sync* semaphore, Sync* fence)
{
    current_index = acquire_impl(this, timeout, semaphore, fence);
    return current_index;
}

Handle<Image> Swapchain::get_image() const { return images.at(current_index); }

ImageView Swapchain::get_view() const { return views.at(current_index); }

// Handle<Mesh> RendererBackendVulkan::instance_mesh(const InstanceSettings& settings)
//{
//     const auto* mr = Engine::get().ecs->get<eng::ecs::Mesh>(settings.entity);
//     const auto* transform = Engine::get().ecs->get<ecs::Transform>(settings.entity);
//     if(!mr) { return {}; }
//     if(!transform) { ENG_ERROR("Instanced node {} doesn't have transform component", settings.entity); }
//     for(const auto& e : mr->meshes)
//     {
//         meshlets_to_instance.push_back(MeshletInstance{ .geometry = e->geometry, .material = e->material, .mesh_idx = instance_index });
//     }
//     ENG_ASSERT(entities.size() == instance_index);
//     entities.push_back(settings.entity);
//     flags.set(RenderFlags::DIRTY_TRANSFORMS_BIT);
//     return Handle<Mesh>{ instance_index++ };
// }

// void RendererBackendVulkan::instance_blas(const BLASInstanceSettings& settings)
//{
//     ENG_TODO("Implement blas instancing");
//     // auto& r = Engine::get().ecs_storage->get<components::Renderable>(settings.entity);
//     // auto& mesh = meshes.at(r.mesh_handle);
//     // auto& geometry = geometries.at(mesh.geometry);
//     // auto& metadata = geometry_metadatas.at(geometry.metadata);
//     // blas_instances.push_back(settings.entity);
//     // flags.set(RenderFlags::DIRTY_TLAS_BIT);
//     // if(!metadata.blas) {
//     //     geometry.flags.set(GeometryFlags::DIRTY_BLAS_BIT);
//     //     flags.set(RenderFlags::DIRTY_BLAS_BIT);
//     // }
// }

// void RendererBackendVulkan::update_transform(ecs::entity entity)
//{
//     // update_positions.push_back(entity);
//     flags.set(RenderFlags::DIRTY_TRANSFORMS_BIT);
// }

// void RendererBackendVulkan::bake_indirect_commands()
//{
//     if(meshlets_to_instance.empty()) { return; }
//
//     std::array<bool, (uint32_t)MeshPassType::LAST_ENUM> recalc{};
//     for(const auto& e : meshlets_to_instance)
//     {
//         const auto& mat = e.material.get();
//         const auto& geom = e.geometry.get();
//         const auto& mpass = mat.mesh_pass.get();
//         for(auto j = 0u; j < mpass.effects.size(); ++j)
//         {
//             if(!mpass.effects.at(j)) { continue; }
//             recalc.at(j) = true;
//             auto& rp = render_passes.at(j);
//             rp.instances.reserve(rp.instances.size() + geom.meshlet_range.offset);
//             for(auto i = 0u; i < geom.meshlet_range.size; ++i)
//             {
//                 rp.instances.push_back(MeshletInstance{ .geometry = e.geometry,
//                                                         .material = e.material,
//                                                         .meshlet_idx = (uint32_t)geom.meshlet_range.offset + i,
//                                                         .mesh_idx = e.mesh_idx });
//             }
//         }
//     }
//
//     for(auto i = 0u; i < recalc.size(); ++i)
//     {
//         if(!recalc.at(i)) { continue; }
//
//         auto& rp = render_passes.at(i);
//         std::sort(rp.instances.begin(), rp.instances.end(), [this](const auto& a, const auto& b) {
//             if(a.material >= b.material) { return false; }       // first sort by material
//             if(a.meshlet_idx >= b.meshlet_idx) { return false; } // then sort by geometry
//             return true;
//         });
//
//         rp.mbatches.resize(rp.instances.size());
//         std::vector<uint32_t> cnts(rp.instances.size());
//         std::vector<DrawIndexedIndirectCommand> cmds(rp.instances.size());
//         std::vector<GPUInstanceId> gpu_ids(rp.instances.size());
//         std::vector<glm::vec4> gpu_bbs(rp.instances.size());
//         Handle<Pipeline> prev_pipeline;
//         uint32_t prev_meshlet = ~0u;
//         uint32_t cmd_off = ~0u;
//         uint32_t pp_off = ~0u;
//         for(auto j = 0u; j < rp.instances.size(); ++j)
//         {
//             const auto& mi = rp.instances.at(j);
//             const auto& ml = meshlets.at(mi.meshlet_idx);
//             const auto& mp = mi.material->mesh_pass.get();
//             const auto& p = mp.effects.at(i)->pipeline;
//             if(prev_pipeline != p)
//             {
//                 prev_pipeline = p;
//                 ++pp_off;
//                 rp.mbatches.at(pp_off).pipeline = p;
//             }
//             if(prev_meshlet != mi.meshlet_idx)
//             {
//                 const auto& g = mi.geometry.get();
//                 prev_meshlet = mi.meshlet_idx;
//                 ++cmd_off;
//                 cmds.at(cmd_off) = DrawIndexedIndirectCommand{ .indexCount = ml.index_count,
//                                                         .instanceCount = 0,
//                                                         .firstIndex = (uint32_t)g.index_range.offset + ml.index_offset,
//                                                         .vertexOffset = (int32_t)(g.vertex_range.offset + ml.vertex_offset),
//                                                         .firstInstance = j };
//             }
//             ++rp.mbatches.at(pp_off).count;
//             ++cnts.at(pp_off);
//             gpu_ids.at(j) = GPUInstanceId{ .batch_id = cmd_off, .instidx = mi.mesh_idx, .material = ~0u };
//             gpu_bbs.at(j) = ml.bounding_sphere;
//         }
//         ++pp_off;
//         ++cmd_off;
//         cnts.reserve(pp_off);
//         cmds.resize(cmd_off);
//         rp.mbatches.resize(pp_off);
//         rp.cmd_count = cmd_off;
//
//         // const auto gpu_cmd_count = cmd_off; // +1
//         const auto post_cull_tri_count = 0u;
//         const auto meshlet_instance_count = (uint32_t)rp.instances.size();
//         const auto cmdoff = align_up2(pp_off * sizeof(uint32_t), 8ull);
//         staging_buf->copy(rp.cmd_buf, cnts, 0);
//         staging_buf->copy(rp.cmd_buf, &post_cull_tri_count, 4, { 0, 4 });
//         staging_buf->copy(rp.cmd_buf, cmds, cmdoff);
//         staging_buf->copy(rp.ids_buf, &meshlet_instance_count, 0, { 0, 4 });
//         staging_buf->copy(rp.ids_buf, gpu_ids, 8);
//         staging_buf->copy(bufs.cull_bs_buf, gpu_bbs, 0);
//     }
//     meshlets_to_instance.clear();
// }
//
// void RendererBackendVulkan::build_transforms_buffer()
//{
//     std::vector<glm::mat4> ts(entities.size());
//     size_t off = 0;
//     for(auto e : entities)
//     {
//         auto* t = Engine::get().ecs->get<ecs::Transform>(e);
//         ts.at(off++) = t->global;
//     }
//     std::swap(bufs.trs_bufs[0], bufs.trs_bufs[1]);
//     staging_buf->copy(bufs.trs_bufs[0], ts, 0ull);
// }

// void RendererBackendVulkan::build_blas()
//{
//     ENG_TODO("IMPLEMENT BACK");
//     return;
// #if 0
//     auto triangles = Vks(VkAccelerationStructureGeometryTrianglesDataKHR{
//         .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
//         .vertexData = { .deviceAddress = get_buffer(vertex_positions_buffer).bda },
//         .vertexStride = sizeof(glm::vec3),
//         .maxVertex = get_total_vertices() - 1u,
//         .indexType = VK_INDEX_TYPE_UINT32,
//         .indexData = { .deviceAddress = get_buffer(index_buffer).bda },
//     });
//
//     std::vector<const Geometry*> dirty_batches;
//     std::vector<VkAccelerationStructureGeometryKHR> blas_geos;
//     std::vector<VkAccelerationStructureBuildGeometryInfoKHR> blas_geo_build_infos;
//     std::vector<uint32_t> scratch_sizes;
//     std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
//     Handle<Buffer> scratch_buffer;
//
//     blas_geos.reserve(geometries.size());
//
//     for(auto& geometry : geometries) {
//         if(!geometry.flags.test_clear(GeometryFlags::DIRTY_BLAS_BIT)) { continue; }
//         GeometryMetadata& meta = geometry_metadatas.at(geometry.metadata);
//         dirty_batches.push_back(&geometry);
//
//         VkAccelerationStructureGeometryKHR& blas_geo = blas_geos.emplace_back();
//         blas_geo = Vks(VkAccelerationStructureGeometryKHR{
//             .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
//             .geometry = { .triangles = triangles },
//             .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
//         });
//
//         VkAccelerationStructureBuildGeometryInfoKHR& build_geometry = blas_geo_build_infos.emplace_back();
//         build_geometry = Vks(VkAccelerationStructureBuildGeometryInfoKHR{
//             .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
//             .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
//             .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
//             .geometryCount = 1,
//             .pGeometries = &blas_geo,
//         });
//
//         const uint32_t primitive_count = geometry.index_count / 3u;
//         auto build_size_info = Vks(VkAccelerationStructureBuildSizesInfoKHR{});
//         vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_geometry,
//                                                 &primitive_count, &build_size_info);
//
//         meta.blas_buffer =
//             make_buffer("blas_buffer", build_size_info.accelerationStructureSize,
//                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
//         scratch_sizes.push_back(align_up2(build_size_info.buildScratchSize,
//                                          static_cast<VkDeviceSize>(rt_acc_props.minAccelerationStructureScratchOffsetAlignment)));
//
//         auto blas_info = Vks(VkAccelerationStructureCreateInfoKHR{
//             .buffer = get_buffer(meta.blas_buffer).buffer,
//             .size = build_size_info.accelerationStructureSize,
//             .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
//         });
//         VK_CHECK(vkCreateAccelerationStructureKHR(dev, &blas_info, nullptr, &meta.blas));
//     }
//
//     // TODO: make non bindless buffer
//     const auto total_scratch_size = std::accumulate(scratch_sizes.begin(), scratch_sizes.end(), 0ul);
//     scratch_buffer = make_buffer("blas_scratch_buffer", total_scratch_size,
//                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false,
//                                  rt_acc_props.minAccelerationStructureScratchOffsetAlignment);
//
//     for(uint32_t i = 0, scratch_offset = 0; const auto& acc_geoms : blas_geos) {
//         const Geometry& geom = *dirty_batches.at(i);
//         const GeometryMetadata& meta = geometry_metadatas.at(geom.metadata);
//         blas_geo_build_infos.at(i).scratchData.deviceAddress = get_buffer(scratch_buffer).bda + scratch_offset;
//         blas_geo_build_infos.at(i).dstAccelerationStructure = meta.blas;
//
//         VkAccelerationStructureBuildRangeInfoKHR& range_info = ranges.emplace_back();
//         range_info = Vks(VkAccelerationStructureBuildRangeInfoKHR{
//             .primitiveCount = geom.index_count / 3u,
//             .primitiveOffset = (uint32_t)((geom.index_offset) * sizeof(uint32_t)),
//             .firstVertex = geom.vertex_offset,
//             .transformOffset = 0,
//         });
//         scratch_offset += scratch_sizes.at(i);
//         ++i;
//     }
//
//     std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> poffsets(ranges.size());
//     for(uint32_t i = 0; i < ranges.size(); ++i) {
//         poffsets.at(i) = &ranges.at(i);
//     }
//
//     auto cmd = get_frame_data().cmdpool->begin_onetime();
//     vkCmdBuildAccelerationStructuresKHR(cmd, blas_geo_build_infos.size(), blas_geo_build_infos.data(), poffsets.data());
//     get_frame_data().cmdpool->end(cmd);
//     Sync f{ dev, false };
//     gq.submit(cmd, &f);
//     f.wait_cpu();
// #endif
// }
//
// void RendererBackendVulkan::build_tlas()
//{
//     return;
//     // std::vector<uint32_t> tlas_mesh_offsets;
//     // std::vector<uint32_t> blas_mesh_offsets;
//     // std::vector<uint32_t> triangle_geo_inst_ids;
//     // std::vector<VkAccelerationStructureInstanceKHR> tlas_instances;
//
//     // std::sort(blas_instances.begin(), blas_instances.end(), [](auto a, auto b) {
//     //     const auto& ra = Engine::get().ecs_storage->get<components::Renderable>(a);
//     //     const auto& rb = Engine::get().ecs_storage->get<components::Renderable>(b);
//     //     return ra.mesh_handle < rb.mesh_handle;
//     // });
//
//     ENG_ASSERT(false);
//
//// TODO: Compress mesh ids per triangle for identical blases with identical materials
//// TODO: Remove geometry offset for indexing in shaders as all blases have only one geometry always
// #if 0
//     for(uint32_t i = 0, toff = 0, boff = 0; i < blas_instances.size(); ++i) {
//         const uint32_t mi_idx = mesh_instance_idxs.at(blas_instances.at(i));
//         const auto& mr = Engine::get().ecs_storage->get<components::Renderable>(mesh_instances.at(mi_idx));
//         const Mesh& mb = meshes.at(mr.mesh_handle);
//         const Geometry& geom = geometries.at(mb.geometry);
//         const MeshMetadata& mm = mesh_metadatas.at(mb.metadata);
//         /*std::distance(mesh_instances.begin(),
//                       std::find_if(mesh_instances.begin(), mesh_instances.end(),
//                                    [&bi](const RenderInstance& e) { return e.handle == bi.instance_handle; }));*/
//
//         triangle_geo_inst_ids.reserve(triangle_geo_inst_ids.size() + geom.index_count / 3u);
//         for(uint32_t j = 0; j < geom.index_count / 3u; ++j) {
//             triangle_geo_inst_ids.push_back(mi_idx);
//         }
//
//         VkAccelerationStructureInstanceKHR& tlas_instance = tlas_instances.emplace_back();
//         tlas_instance = Vks(VkAccelerationStructureInstanceKHR{
//             .transform = std::bit_cast<VkTransformMatrixKHR>(glm::transpose(glm::mat4x3{
//                 Engine::get().ecs_storage->get<components::Transform>(mesh_instances.at(mi_idx)).transform })),
//             .instanceCustomIndex = 0,
//             .mask = 0xFF,
//             .instanceShaderBindingTableRecordOffset = 0,
//             .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
//             .accelerationStructureReference = geometry_metadatas.at(geom.metadata).blas_buffer.bda,
//         });
//         tlas_mesh_offsets.push_back(toff);
//         blas_mesh_offsets.push_back(boff / 3u);
//         ++toff;
//         boff += geom.index_count; // TODO: validate this
//     }
//     // tlas_mesh_offsets_buffer = Buffer{ "tlas mesh offsets", tlas_mesh_offsets.size() * sizeof(tlas_mesh_offsets[0]),
//     //                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
//     // tlas_mesh_offsets_buffer.push_data(tlas_mesh_offsets);
//     // blas_mesh_offsets_buffer = Buffer{ "blas mesh offsets", blas_mesh_offsets.size() * sizeof(blas_mesh_offsets[0]),
//     //                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
//     // blas_mesh_offsets_buffer.push_data(blas_mesh_offsets);
//     // triangle_geo_inst_id_buffer =
//     //     Buffer{ "triangle geo inst id", triangle_geo_inst_ids.size() * sizeof(triangle_geo_inst_ids[0]),
//     //             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
//     // triangle_geo_inst_id_buffer.push_data(triangle_geo_inst_ids);
//     // tlas_instance_buffer =
//     //     Buffer{ "tlas_instance_buffer", sizeof(tlas_instances[0]) * tlas_instances.size(), 16u,
//     //             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
//     //             false };
//     // tlas_instance_buffer.push_data(tlas_instances);
//
//     auto geometry = Vks(VkAccelerationStructureGeometryKHR{
//         .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
//         .geometry = { .instances = Vks(VkAccelerationStructureGeometryInstancesDataKHR{
//                           .arrayOfPointers = false,
//                           .data = { .deviceAddress = tlas_instance_buffer.bda },
//                       }) },
//         .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
//     });
//
//     auto tlas_info = Vks(VkAccelerationStructureBuildGeometryInfoKHR{
//         .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
//         .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
//         .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
//         .geometryCount = 1, // must be 1 for TLAS
//         .pGeometries = &geometry,
//     });
//
//     auto build_size = Vks(VkAccelerationStructureBuildSizesInfoKHR{});
//     const uint32_t max_primitives = tlas_instances.size();
//     vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_info,
//                                             &max_primitives, &build_size);
//
//     /*  tlas_buffer =
//           Buffer{ "tlas_buffer", build_size.accelerationStructureSize,
//                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false };
//
//       auto acc_info = Vks(VkAccelerationStructureCreateInfoKHR{
//           .buffer = tlas_buffer.buffer,
//           .size = build_size.accelerationStructureSize,
//           .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
//       });
//       vkCreateAccelerationStructureKHR(dev, &acc_info, nullptr, &tlas);
//
//       tlas_scratch_buffer = Buffer{ "tlas_scratch_buffer", build_size.buildScratchSize,
//                                     rt_acc_props.minAccelerationStructureScratchOffsetAlignment,
//                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false };
//
//       tlas_info.dstAccelerationStructure = tlas;
//       tlas_info.scratchData.deviceAddress = tlas_scratch_buffer.bda;
//
//       auto build_range = Vks(VkAccelerationStructureBuildRangeInfoKHR{
//           .primitiveCount = max_primitives,
//           .primitiveOffset = 0,
//           .firstVertex = 0,
//           .transformOffset = 0,
//       });
//       VkAccelerationStructureBuildRangeInfoKHR* build_ranges[]{ &build_range };
//
//       auto cmd = get_frame_data().cmdpool->begin_onetime();
//       vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_info, build_ranges);
//       get_frame_data().cmdpool->end(cmd);
//       Fence f{ dev, false };
//       gq.submit(cmd, &f);
//       f.wait();*/
// #endif
// }
//
// void RendererBackendVulkan::update_ddgi()
//{
// #if 0
//     // ENG_ASSERT(false && "no allocating new pointers for images");
//
//     BoundingBox scene_aabb{ .min = glm::vec3{ -2.0f }, .max = glm::vec3{ 2.0f } };
//     /*for(const Node& node : Engine::scene()->nodes) {
//         if(!node.has_component<cmps::Mesh>()) { continue; }
//         const cmps::Mesh& rm = Engine::ec()->get<cmps::Mesh>(node.handle);
//         glm::mat4 t = Engine::scene()->get_final_transform(node.handle);
//         BoundingBox m = rm.mesh->aabb;
//         m.min = m.min * glm::mat4x3{ t };
//         m.max = m.max * glm::mat4x3{ t };
//         scene_aabb.min = glm::min(scene_aabb.min, m.min);
//         scene_aabb.max = glm::max(scene_aabb.max, m.max);
//     }*/
//
//     ddgi.probe_dims = scene_aabb;
//     const auto dim_scaling = glm::vec3{ 0.95, 0.7, 0.95 };
//     ddgi.probe_dims.min *= dim_scaling;
//     ddgi.probe_dims.max *= dim_scaling;
//     ddgi.probe_distance = 1.5f;
//
//     ddgi.probe_counts = ddgi.probe_dims.size() / ddgi.probe_distance;
//     ddgi.probe_counts = { std::bit_ceil(ddgi.probe_counts.x), std::bit_ceil(ddgi.probe_counts.y),
//                           std::bit_ceil(ddgi.probe_counts.z) };
//     // ddgi.probe_counts = {4, 2, 4};
//     const auto num_probes = ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z;
//
//     ddgi.probe_walk = ddgi.probe_dims.size() / glm::vec3{ glm::max(ddgi.probe_counts, glm::uvec3{ 2u }) - glm::uvec3(1u) };
//     // ddgi.probe_walk = {ddgi.probe_walk.x, 4.0f, ddgi.probe_walk.z};
//
//     const uint32_t irradiance_texture_width = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
//     const uint32_t irradiance_texture_height = (ddgi.irradiance_probe_side + 2) * ddgi.probe_counts.z;
//     const uint32_t visibility_texture_width = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.x * ddgi.probe_counts.y;
//     const uint32_t visibility_texture_height = (ddgi.visibility_probe_side + 2) * ddgi.probe_counts.z;
//
//     *ddgi.radiance_texture = Image{ "ddgi radiance",
//                                     ddgi.rays_per_probe,
//                                     ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z,
//                                     1,
//                                     1,
//                                     1,
//                                     VK_FORMAT_R16G16B16A16_SFLOAT,
//                                     VK_SAMPLE_COUNT_1_BIT,
//                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
//     *ddgi.irradiance_texture = Image{
//         "ddgi irradiance", irradiance_texture_width, irradiance_texture_height, 1, 1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
//     };
//     *ddgi.visibility_texture = Image{
//         "ddgi visibility", visibility_texture_width, visibility_texture_height, 1, 1, 1, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
//     };
//
//     *ddgi.probe_offsets_texture = Image{ "ddgi probe offsets",
//                                          ddgi.probe_counts.x * ddgi.probe_counts.y,
//                                          ddgi.probe_counts.z,
//                                          1,
//                                          1,
//                                          1,
//                                          VK_FORMAT_R16G16B16A16_SFLOAT,
//                                          VK_SAMPLE_COUNT_1_BIT,
//                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
//
//     auto cmd = get_frame_data().cmdpool->begin_onetime();
//     ddgi.radiance_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
//                                              VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
//                                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
//     ddgi.irradiance_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
//                                                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
//                                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
//     ddgi.visibility_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
//                                                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
//                                                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
//     ddgi.probe_offsets_texture->transition_layout(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, PipelineAccess::NONE,
//                                                   VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
//                                                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, ImageLayout::GENERAL);
//     get_frame_data().cmdpool->end(cmd);
//     gq.submit(cmd);
//
//     ddgi.buffer = Buffer{ "ddgi_settings_buffer", sizeof(DDGI::GPULayout),
//                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };
//     ddgi.debug_probe_offsets_buffer =
//         Buffer{ "ddgi debug probe offsets buffer", sizeof(DDGI::GPUProbeOffsetsLayout) * num_probes,
//                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, true };
//
//     DDGI::GPULayout ddgi_gpu_settings{
//         .radiance_tex_size = glm::ivec2{ ddgi.radiance_texture->width, ddgi.radiance_texture->height },
//         .irradiance_tex_size = glm::ivec2{ ddgi.irradiance_texture->width, ddgi.irradiance_texture->height },
//         .visibility_tex_size = glm::ivec2{ ddgi.visibility_texture->width, ddgi.visibility_texture->height },
//         .probe_offset_tex_size = glm::ivec2{ ddgi.probe_offsets_texture->width, ddgi.probe_offsets_texture->height },
//         .probe_start = ddgi.probe_dims.min,
//         .probe_counts = ddgi.probe_counts,
//         .probe_walk = ddgi.probe_walk,
//         .min_probe_distance = 0.01f,
//         .max_probe_distance = 20.0f,
//         .min_dist = 0.1f,
//         .max_dist = 20.0f,
//         .normal_bias = 0.08f,
//         .max_probe_offset = 0.5f,
//         .frame_num = 0,
//         .irradiance_probe_side = ddgi.irradiance_probe_side,
//         .visibility_probe_side = ddgi.visibility_probe_side,
//         .rays_per_probe = ddgi.rays_per_probe,
//         .debug_probe_offsets = ddgi.debug_probe_offsets_buffer.bda
//     };
//
//     if(ddgi.probe_counts.y == 1) {
//         ddgi_gpu_settings.probe_start.y += ddgi.probe_walk.y * 0.5f;
//         ddgi.probe_start.y += ddgi.probe_walk.y * 0.5f;
//     }
//
//     ddgi.buffer.push_data(&ddgi_gpu_settings, sizeof(DDGI::GPULayout), 0);
//     submit_queue->wait_idle();
// #endif
// }

// void RendererBackendVulkan::process_request(const RenderRequest& req)
//{
//     if(!req.cmd)
//     {
//         ENG_WARN("Cmd is empty");
//         return;
//     }
//
//     const auto& rp = render_passes.at((uint32_t)req.mesh_pass);
//     if(req.compute) { req.cmd->dispatch((rp.cmd_count + 31) / 32, 1, 1); }
//     else
//     {
//         for(auto i = 0u; i < rp.mbatches.size(); ++i)
//         {
//             const auto cntoff = 0;
//             const auto cntsz = sizeof(uint32_t);
//             const auto cmdoff = align_up2(rp.mbatches.size() * cntsz, 8ull);
//             const auto engcbi = get_bindless(bufs.const_bufs[0]);
//             req.cmd->bind_pipeline(rp.mbatches.at(i).pipeline.get());
//             req.cmd->push_constants(ShaderStage::ALL, &engcbi, { 0, sizeof(engcbi) });
//             req.cmd->bind_index(bufs.idx_buf.get(), 0, bufs.index_type);
//             req.cmd->draw_indexed_indirect_count(rp.cmd_buf.get(), cmdoff, rp.cmd_buf.get(), cntsz * i, rp.cmd_count,
//                                                  sizeof(DrawIndexedIndirectCommand));
//         }
//     }
// }

// void RendererBackendVulkan::destroy_buffer(Handle<Buffer> buffer)
//{
//     VkBufferMetadata::destroy(buffer.get());
//     buffers.erase(buffer);
// }
//
// void gfx::RendererBackendVulkan::destroy_image(Handle<Image> image)
//{
//     VkImageMetadata::destroy(image.get());
//     images.erase(image);
// }
//
// void RendererBackendVulkan::destroy_view(ImageView view)
//{
//     VkImageViewMetadata::destroy(view.get());
//     image_views.erase(view);
// }
//
// uint32_t RendererBackendVulkan::get_bindless(Handle<Buffer> buffer) { return bindless_pool->get_index(buffer); }
//
// void RendererBackendVulkan::update_resource(Handle<Buffer> dst) { bindless_pool->update_index(dst); }
//
// FrameData& RendererBackendVulkan::get_frame_data(uint32_t offset)
//{
//     return frame_datas[(Engine::get().frame_num + offset) % frame_datas.size()];
// }
//
// const FrameData& RendererBackendVulkan::get_frame_data(uint32_t offset) const
//{
//     return const_cast<RendererBackendVulkan*>(this)->get_frame_data();
// }

} // namespace gfx

} // namespace eng
