#include "renderer_vulkan_wrappers.hpp"
#include "engine.hpp"
#include "renderer_vulkan.hpp"
#include "set_debug_name.hpp"

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

void Image::_create_default_view(int dims) {
    --dims;
    VkImageViewType view_types[]{ VK_IMAGE_VIEW_TYPE_1D, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_VIEW_TYPE_3D };
    const auto aspect = (this->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : this->aspect;
    const auto ivinfo = Vks(VkImageViewCreateInfo{
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

VkSampler SamplerStorage::get_sampler(ImageFilter filter, ImageAddressing address) {
    const VkFilter _filter = filter == ImageFilter::LINEAR    ? VK_FILTER_LINEAR
                             : filter == ImageFilter::NEAREST ? VK_FILTER_NEAREST
                                                              : VK_FILTER_MAX_ENUM;
    const VkSamplerAddressMode _address = address == ImageAddressing::REPEAT  ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                          : address == ImageAddressing::CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                                              : VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    return get_sampler(_filter, _address);
}

VkSampler SamplerStorage::get_sampler(VkSamplerCreateInfo info) {
    for(const auto& s : samplers) {
        if(s.first.magFilter == info.magFilter && s.first.minFilter == info.minFilter && s.first.addressModeU == info.addressModeU) {
            return s.second;
        }
    }
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(get_renderer().dev, &info, nullptr, &sampler));
    samplers.emplace_back(info, sampler);
    return sampler;
}

CommandPool::CommandPool(uint32_t queue_index, VkCommandPoolCreateFlags flags) noexcept {
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
