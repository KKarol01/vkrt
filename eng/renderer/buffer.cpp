#include <eng/renderer/buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/common/logger.hpp>

namespace gfx
{

Buffer::Buffer(VkDevice dev, VmaAllocator vma, const BufferCreateInfo& create_info) noexcept
    : dev(dev), vma(vma), create_info(create_info)
{
    assert(dev && vma);
    if(create_info.size > 0) { allocate(); }
}

void Buffer::allocate()
{
    if(!dev || !vma)
    {
        ENG_WARN("Device or vma allocator are null. Cannot allocate.");
        return;
    }
    if(buffer)
    {
        ENG_WARN("Allocating already allocated buffer.");
        return;
    }
    if(create_info.size == 0)
    {
        ENG_WARN("Cannot create buffer of size 0.");
        return;
    }

    auto* r = RendererVulkan::get_instance();
    const auto vkinfo = Vks(VkBufferCreateInfo{
        .size = create_info.size, .usage = create_info.usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
    const auto vmainfo = VmaAllocationCreateInfo{
        .flags = create_info.mapped ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = create_info.mapped ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0u
    };

    VmaAllocationInfo vma_ai{};
    VK_CHECK(vmaCreateBuffer(vma, &vkinfo, &vmainfo, &buffer, &vma_alloc, &vma_ai));
    if(buffer) { set_debug_name(buffer, create_info.name); }
    else
    {
        ENG_WARN("Could not create a buffer");
        return;
    }
    mapped = vma_ai.pMappedData;
    if(vkinfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const auto bda_info = Vks(VkBufferDeviceAddressInfo{ .buffer = buffer });
        bda = vkGetBufferDeviceAddress(dev, &bda_info);
    }
}

void Buffer::deallocate() {}

void Buffer::resize(size_t sz) {}

} // namespace gfx
