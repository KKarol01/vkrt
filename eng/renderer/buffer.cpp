#include <eng/renderer/buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/staging_buffer.hpp>
#include <eng/renderer/bindlesspool.hpp>
#include <eng/common/logger.hpp>

namespace gfx
{

Buffer::Buffer(VkDevice dev, VmaAllocator vma, const BufferCreateInfo& create_info) noexcept
    : dev(dev), vma(vma), name(create_info.name), usage(create_info.usage), capacity(create_info.size), memory(nullptr),
      mapped(create_info.mapped)
{
    assert(dev && vma);
    allocate();
}

Buffer::Buffer(Buffer&& o) noexcept { *this = std::move(o); }

Buffer& Buffer::operator=(Buffer&& o) noexcept
{
    deallocate();
    name = std::move(o.name);
    dev = std::exchange(o.dev, nullptr);
    buffer = std::exchange(o.buffer, nullptr);
    vma = std::exchange(o.vma, nullptr);
    vmaalloc = std::exchange(o.vmaalloc, nullptr);
    bda = std::exchange(o.bda, VkDeviceAddress{});
    usage = o.usage;
    capacity = std::exchange(o.capacity, 0);
    size = std::exchange(o.size, 0);
    memory = std::exchange(o.memory, nullptr);
    bindless_index = o.bindless_index;
    mapped = o.mapped;
    return *this;
}

void Buffer::allocate()
{
    if(capacity == 0) { return; }
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

    auto* r = RendererVulkan::get_instance();
    const auto vkinfo = Vks(VkBufferCreateInfo{
        .size = capacity, .usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
    const auto vmainfo = VmaAllocationCreateInfo{
        .flags = mapped ? VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = mapped ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : 0u
    };

    VmaAllocationInfo vmaai{};
    VK_CHECK(vmaCreateBuffer(vma, &vkinfo, &vmainfo, &buffer, &vmaalloc, &vmaai));
    if(buffer) { set_debug_name(buffer, name); }
    else
    {
        ENG_WARN("Could not create a buffer");
        return;
    }
    memory = vmaai.pMappedData;
    if(vkinfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const auto vkbdai = Vks(VkBufferDeviceAddressInfo{ .buffer = buffer });
        bda = vkGetBufferDeviceAddress(dev, &vkbdai);
    }
    bindless_index = r->bindless_pool->allocate_buffer_index();
    r->bindless_pool->update_index(bindless_index, buffer);
}

void Buffer::deallocate()
{
    auto* r = RendererVulkan::get_instance();
    if(!buffer || !vmaalloc) { return; }
    if(bindless_index != ~0ul) { r->bindless_pool->free_buffer_index(bindless_index); }
    if(memory) { vmaUnmapMemory(vma, vmaalloc); }
    vmaDestroyBuffer(vma, buffer, vmaalloc);
    *this = Buffer{};
}

} // namespace gfx
