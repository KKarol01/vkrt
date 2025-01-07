#include "renderer_vulkan_wrappers.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"

Buffer::Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map)
    : Buffer(name, size, 1u, usage, map) {}

Buffer::Buffer(const std::string& name, size_t size, uint32_t alignment, VkBufferUsageFlags usage, bool map)
    : Buffer(name, Vks(VkBufferCreateInfo{ .size = size, .usage = usage }),
             VmaAllocationCreateInfo{
                 .flags = (map ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u),
                 .usage = VMA_MEMORY_USAGE_AUTO,
             },
             alignment) {}

Buffer::Buffer(const std::string& name, VkBufferCreateInfo create_info, VmaAllocationCreateInfo alloc_info, uint32_t alignment)
    : name{ name }, capacity{ create_info.size }, alignment{ alignment } {
    uint32_t queue_family_indices[]{ get_renderer().gq.idx, get_renderer().gq.idx };
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
        auto bdainfo = Vks(VkBufferDeviceAddressInfo{
            .buffer = buffer,
        });
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

bool Buffer::push_data(std::span<const std::byte> data, uint32_t offset) {
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
        auto cmd = get_renderer().get_frame_data().cmdpool->begin_onetime();
        uint32_t iters = static_cast<uint32_t>(std::ceilf(static_cast<float>(data.size_bytes()) / 65536.0f));
        for(uint64_t off = 0, i = 0; i < iters; ++i) {
            const auto size = std::min(data.size_bytes() - off, 65536ull);
            vkCmdUpdateBuffer(cmd, buffer, offset + off, size, data.data() + off);
            off += size;
        }
        get_renderer().get_frame_data().cmdpool->end(cmd);
        get_renderer().gq.submit(cmd);
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
        auto cmd = get_renderer().get_frame_data().cmdpool->begin_onetime();
        VkBufferCopy region{ .size = size };
        vkCmdCopyBuffer(cmd, buffer, new_buffer.buffer, 1, &region);
        get_renderer().get_frame_data().cmdpool->end(cmd);
        get_renderer().gq.submit(cmd);
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

Image::Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers,
             VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage)
    : format(format), mips(mips), layers(layers), width(width), height(height), depth(depth), usage(usage) {

    int dims = -1;
    if(width > 1) { ++dims; }
    if(height > 1) { ++dims; }
    if(depth > 1) { ++dims; }
    if(dims == -1) { dims = 1; }
    VkImageType types[]{ VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_2D, VK_IMAGE_TYPE_3D };

    auto iinfo = Vks(VkImageCreateInfo{
        .flags = {},
        .imageType = types[dims],
        .format = format,
        .extent = { width, height, depth },
        .mipLevels = mips,
        .arrayLayers = layers,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });

    VmaAllocationCreateInfo vmainfo{
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VK_CHECK(vmaCreateImage(get_renderer().vma, &iinfo, &vmainfo, &image, &alloc, nullptr));
    _deduce_aspect(usage);
    _create_default_view(dims, usage);

    set_debug_name(image, name);
    set_debug_name(view, std::format("{}_default_view", name));
}

Image::Image(const std::string& name, VkImage image, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips,
             uint32_t layers, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage)
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
    if(image) { vkDestroyImage(get_renderer().dev, image, nullptr); }
    if(view) { vkDestroyImageView(get_renderer().dev, view, nullptr); }
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
    auto imgb = Vks(VkImageMemoryBarrier2{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = src_layout,
        .newLayout = dst_layout,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = mips,
            .baseArrayLayer = 0,
            .layerCount = layers,
        },
    });
    auto dep = Vks(VkDependencyInfo{});
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

    auto ivinfo = Vks(VkImageViewCreateInfo{
        .image = image,
        .viewType = view_types[dims],
        .format = format,
        .components = {},
        .subresourceRange = { .aspectMask = aspect, .baseMipLevel = 0, .levelCount = mips, .baseArrayLayer = 0, .layerCount = 1 },
    });

    VK_CHECK(vkCreateImageView(get_renderer().dev, &ivinfo, nullptr, &view));
}

VkSampler SamplerStorage::get_sampler() {
    auto sampler_info = Vks(VkSamplerCreateInfo{});
    return get_sampler(sampler_info);
}

VkSampler SamplerStorage::get_sampler(VkFilter filter, VkSamplerAddressMode address) {
    auto sampler_info = Vks(VkSamplerCreateInfo{
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = address,
        .addressModeV = address,
        .addressModeW = address,
        .maxLod = 1.0f,
    });
    return get_sampler(sampler_info);
}

VkSampler SamplerStorage::get_sampler(VkSamplerCreateInfo info) {
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(get_renderer().dev, &info, nullptr, &sampler));
    samplers.emplace_back(info, sampler);
    return sampler;
}

CommandPool::CommandPool(uint32_t queue_index, VkCommandPoolCreateFlags flags) {
    auto info = Vks(VkCommandPoolCreateInfo{
        .flags = flags,
        .queueFamilyIndex = queue_index,
    });
    VK_CHECK(vkCreateCommandPool(get_renderer().dev, &info, {}, &cmdpool));
}

CommandPool::~CommandPool() noexcept {
    if(cmdpool) { vkDestroyCommandPool(get_renderer().dev, cmdpool, nullptr); }
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

    auto info = Vks(VkCommandBufferAllocateInfo{
        .commandPool = cmdpool,
        .level = level,
        .commandBufferCount = 1,
    });
    VkCommandBuffer buffer;
    VK_CHECK(vkAllocateCommandBuffers(get_renderer().dev, &info, &buffer));
    buffers.emplace_back(buffer, false);
    return buffer;
}

VkCommandBuffer CommandPool::begin(VkCommandBufferUsageFlags flags, VkCommandBufferLevel level) {
    auto info = Vks(VkCommandBufferBeginInfo{
        .flags = flags,
    });
    VkCommandBuffer buffer = allocate(level);
    VK_CHECK(vkBeginCommandBuffer(buffer, &info));
    return buffer;
}

VkCommandBuffer CommandPool::begin_onetime(VkCommandBufferLevel level) {
    return begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void CommandPool::end(VkCommandBuffer buffer) { VK_CHECK(vkEndCommandBuffer(buffer)); }

void CommandPool::reset() {
    vkResetCommandPool(get_renderer().dev, cmdpool, {});
    for(auto& e : buffers) {
        e.second = true;
    }
}
