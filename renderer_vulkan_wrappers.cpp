#include "renderer_vulkan_wrappers.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"

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
        auto cmd = get_renderer().frame_data.get().cmdpool->begin_onetime();
        u32 iters = static_cast<u32>(std::ceilf(static_cast<f32>(data.size_bytes()) / 65536.0f));
        for(u64 off = 0, i = 0; i < iters; ++i) {
            const auto size = std::min(data.size_bytes() - off, 65536ull);
            vkCmdUpdateBuffer(cmd, buffer, offset + off, size, data.data() + off);
            off += size;
        }
        get_renderer().frame_data.get().cmdpool->end(cmd);
        vks::CommandBufferSubmitInfo cmdinfo;
        cmdinfo.commandBuffer = cmd;
        vks::SubmitInfo2 info;
        info.commandBufferInfoCount = 1;
        info.pCommandBufferInfos = &cmdinfo;
        vkQueueSubmit2(get_renderer().gq, 1, &info, nullptr);
        /*std::atomic_flag flag{};
        if(!get_renderer().staging->send_to(GpuStagingUpload{ .dst = buffer, .src = data, .dst_offset = offset, .size_bytes = data.size_bytes() },
                                            {}, {}, &flag)) {
            return false;
        }
        flag.wait(false);*/
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
    } else if(size > 0) {
        auto cmd = get_renderer().frame_data.get().cmdpool->begin_onetime();
        VkBufferCopy region{ .size = size };
        vkCmdCopyBuffer(cmd, buffer, new_buffer.buffer, 1, &region);
        get_renderer().frame_data.get().cmdpool->end(cmd);
        vks::CommandBufferSubmitInfo cmdinfo;
        cmdinfo.commandBuffer = cmd;
        vks::SubmitInfo2 info;
        info.commandBufferInfoCount = 1;
        info.pCommandBufferInfos = &cmdinfo;
        vkQueueSubmit2(get_renderer().gq, 1, &info, nullptr);
        flag.test_and_set();
        success = true;
        // assert(false);
        /*success = get_renderer().staging->send_to(GpuStagingUpload{ .dst = new_buffer.buffer, .src = buffer, .size_bytes = size },
                                                  {}, {}, &flag);*/
    } else {
        success = true;
        flag.test_and_set();
    }

    if(!success) { return false; }
    flag.wait(false);

    *this = std::move(new_buffer);
    return true;
}

void Buffer::deallocate() {
    if(buffer && alloc) { vmaDestroyBuffer(get_renderer().vma, buffer, alloc); }
}

Image::Image(const std::string& name, u32 width, u32 height, u32 depth, u32 mips, u32 layers, VkFormat format,
             VkSampleCountFlagBits samples, VkImageUsageFlags usage)
    : format(format), mips(mips), layers(layers), width(width), height(height), depth(depth), usage(usage) {
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
    : image{ image }, format(format), mips(mips), layers(layers), width(width), height(height), depth(depth), usage(usage) {
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
    usage = other.usage;
    return *this;
}

void Image::transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                              VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout dst_layout) {
    transition_layout(cmd, src_stage, src_access, dst_stage, dst_access, current_layout, dst_layout);
}

void Image::transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                              VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout src_layout,
                              VkImageLayout dst_layout) {
    vks::ImageMemoryBarrier2 imgb;
    imgb.image = image;
    imgb.oldLayout = src_layout;
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

RendererPipelineLayoutBuilder& RendererPipelineLayoutBuilder::add_set_binding(u32 set, u32 binding, u32 count, VkDescriptorType type,
                                                                              VkDescriptorBindingFlags binding_flags,
                                                                              VkShaderStageFlags stages) {
    ENG_ASSERT(set < descriptor_layouts.size(), "Trying to access out of bounds descriptor set layout with idx: {}", set);

    if(set > 0 && descriptor_layouts.at(set - 1).bindings.empty()) {
        ENG_WARN("Settings descriptor set layout with idx {} while the previous descriptor set layout ({}) has "
                 "empty bindings.",
                 set, set - 1);
    }

    descriptor_layouts.at(set).bindings.emplace_back(binding, type, count, stages, nullptr);
    descriptor_layouts.at(set).binding_flags.emplace_back(binding_flags);
    return *this;
}

RendererPipelineLayoutBuilder& RendererPipelineLayoutBuilder::add_variable_descriptor_count(u32 set) {
    if(descriptor_layouts.size() <= set) {
        ENG_WARN("Trying to access out of bounds descriptor set layout with idx: {}", set);
        return *this;
    }
    descriptor_layouts.at(set).last_binding_of_variable_count = true;
    return *this;
}

RendererPipelineLayoutBuilder& RendererPipelineLayoutBuilder::set_push_constants(u32 size, VkShaderStageFlags stages) {
    push_constants_size = size;
    push_constants_stage = stages;
    return *this;
}

RendererPipelineLayoutBuilder& RendererPipelineLayoutBuilder::set_layout_flags(u32 set, VkDescriptorSetLayoutCreateFlags layout_flags) {
    ENG_ASSERT(set < descriptor_layouts.size(), "Trying to access out of bounds descriptor set layout with idx: {}", set);
    descriptor_layout_flags.at(set) = layout_flags;
    return *this;
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
    // That's most likely slower than just creating new
    // for(const auto& [c, s] : samplers) {
    //    if(c.magFilter != info.magFilter) { continue; }
    //    if(c.minFilter != info.minFilter) { continue; }
    //    if(c.mipmapMode != info.mipmapMode) { continue; }
    //    if(c.addressModeU != info.addressModeU) { continue; }
    //    if(c.addressModeV != info.addressModeV) { continue; }
    //    if(c.addressModeW != info.addressModeW) { continue; }
    //    if(c.mipLodBias != info.mipLodBias) { continue; }
    //    if(c.anisotropyEnable != info.anisotropyEnable) { continue; }
    //    if(c.maxAnisotropy != info.maxAnisotropy) { continue; }
    //    if(c.compareEnable != info.compareEnable) { continue; }
    //    if(c.compareOp != info.compareOp) { continue; }
    //    if(c.minLod != info.minLod) { continue; }
    //    if(c.maxLod != info.maxLod) { continue; }
    //    if(c.borderColor != info.borderColor) { continue; }
    //    if(c.unnormalizedCoordinates != info.unnormalizedCoordinates) { continue; }
    //    return s;
    //}

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
