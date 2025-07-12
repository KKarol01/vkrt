#include "resources.hpp"
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/common/to_vk.hpp>
#include <eng/common/to_string.hpp>

namespace gfx
{

Buffer::Buffer(const BufferCreateInfo& info) noexcept
    : name(info.name), usage(info.usage), capacity(std::max(info.size, 1024ull)), mapped(info.mapped)
{
}

bool Buffer::operator==(const Buffer& b) const
{
    return buffer == b.buffer && usage == b.usage && capacity == b.capacity && size == b.size && mapped == b.mapped;
}

void Buffer::init()
{
    if(capacity == 0)
    {
        ENG_WARN("Capacity cannot be 0");
        return;
    }

    if(buffer)
    {
        ENG_WARN("Allocating already allocated buffer.");
        return;
    }

    auto* r = RendererVulkan::get_instance();
    const auto vkinfo = Vks(VkBufferCreateInfo{
        .size = capacity, .usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
    const auto vmainfo = VmaAllocationCreateInfo{
        .flags = mapped ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = mapped ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0u
    };

    VmaAllocationInfo vmaai{};
    VK_CHECK(vmaCreateBuffer(r->vma, &vkinfo, &vmainfo, &buffer, &vmaa, &vmaai));
    if(buffer) { set_debug_name(buffer, name); }
    else
    {
        ENG_WARN("Could not create buffer {}", name);
        return;
    }
    memory = vmaai.pMappedData;
    if(vkinfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const auto vkbdai = Vks(VkBufferDeviceAddressInfo{ .buffer = buffer });
        bda = vkGetBufferDeviceAddress(r->dev, &vkbdai);
    }
}

void Buffer::destroy()
{
    auto* r = RendererVulkan::get_instance();
    if(!buffer || !vmaa) { return; }
    if(memory) { vmaUnmapMemory(r->vma, vmaa); }
    vmaDestroyBuffer(r->vma, buffer, vmaa);
    *this = Buffer{};
}

Image::Image(const std::string& name, VkImage image, VmaAllocation vmaa, VkImageLayout current_layout,
             VkExtent3D extent, VkFormat format, uint32_t mips, uint32_t layers, VkImageUsageFlags usage) noexcept
    : name(name), image(image), vmaa(vmaa), current_layout(current_layout), extent(extent), format(format), mips(mips),
      layers(layers), usage(usage)
{
}

Image::Image(const ImageCreateInfo& info) noexcept
    : name(info.name), current_layout(info.current_layout), extent(info.extent), format(info.format), mips(info.mips),
      layers(info.layers), usage(info.usage)
{
}

bool Image::operator==(const Image& b) const
{
    return image == b.image && current_layout == b.current_layout && extent.width == b.extent.width &&
           extent.height == b.extent.height && extent.depth == b.extent.depth && format == b.format && mips == b.mips &&
           layers == b.layers && usage == b.usage;
}

void Image::init()
{
    if(image)
    {
        ENG_WARN("Allocating already allocated buffer.");
        return;
    }
    if(extent.width + extent.height + extent.depth == 0)
    {
        ENG_WARN("Trying to create 0-sized image");
        return;
    }

    auto* r = RendererVulkan::get_instance();
    VmaAllocationCreateInfo vma_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    const auto info = Vks(VkImageCreateInfo {
        .imageType = deduce_image_type(),
        .format = format,
        .extent = {
            std::max(extent.width, 1u),
            std::max(extent.height, 1u),
            std::max(extent.depth, 1u),
        },
        .mipLevels = mips,
        .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .initialLayout = current_layout
    });
    VK_CHECK(vmaCreateImage(r->vma, &info, &vma_info, &image, &vmaa, nullptr));
    if(image)
    {
        set_debug_name(image, name);
        create_image_view(ImageViewDescriptor{ .name = fmt::format("{}_default_view", name) });
    }
    else { ENG_WARN("Could not create image {}", name); }
}

void Image::destroy()
{
    auto* r = RendererVulkan::get_instance();
    if(!image || !vmaa) { return; }
    for(const auto& [info, view] : views)
    {
        vkDestroyImageView(r->dev, view, nullptr);
    }
    views.clear();
    vmaDestroyImage(r->vma, image, vmaa);
    *this = Image{};
}

VkImageAspectFlags Image::deduce_aspect() const
{
    if(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkImageType Image::deduce_image_type() const
{
    return extent.depth > 0 ? VK_IMAGE_TYPE_3D : extent.height > 0 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D;
}

VkImageViewType Image::deduce_image_view_type() const
{
    return extent.depth > 0 ? VK_IMAGE_VIEW_TYPE_3D : extent.height > 0 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D;
}

VkImageView Image::create_image_view(const ImageViewDescriptor& info)
{
    if(const auto it = views.find(info); it != views.end()) { return it->second; }
    auto* r = RendererVulkan::get_instance();
    const auto vkinfo = Vks(VkImageViewCreateInfo{
        .image = image,
        .viewType = info.view_type ? eng::to_vk(*info.view_type) : deduce_image_view_type(),
        .format = info.format ? eng::to_vk(*info.format) : format,
        .subresourceRange = { deduce_aspect(), (uint32_t)info.mips.offset, (uint32_t)info.mips.size,
                              (uint32_t)info.layers.offset, (uint32_t)info.layers.size } });
    VkImageView view{};
    VK_CHECK(vkCreateImageView(r->dev, &vkinfo, {}, &view));
    if(view)
    {
        set_debug_name(view, info.name);
        views[info] = view;
    }
    else { ENG_WARN("Could not create image view {}", info.name); }
    return view;
}

VkImageView Image::get_image_view(const ImageViewDescriptor& info) const { return views.at(info); }

} // namespace gfx
