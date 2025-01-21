#include "renderer_vulkan_wrappers.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"

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
    if(free.empty()) {
        auto info = Vks(VkCommandBufferAllocateInfo{
            .commandPool = cmdpool,
            .level = level,
            .commandBufferCount = 1,
        });
        VkCommandBuffer buffer;
        VK_CHECK(vkAllocateCommandBuffers(get_renderer().dev, &info, &buffer));
        used.push_back(buffer);
    } else {
        used.push_back(free.front());
        free.pop_front();
    }
    return used.back();
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
    free = std::move(used);
}
