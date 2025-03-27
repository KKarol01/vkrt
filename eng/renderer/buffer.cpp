#include <eng/renderer/buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

Buffer::Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, const VkBufferCreateInfo& vk_info,
               const VmaAllocationCreateInfo& vma_info) noexcept
    : name(name), dev(dev), vma(vma), vk_info(vk_info), vma_info(vma_info) {
    if(!dev || !vma) { return; }
    VmaAllocationInfo alloc_info{};
    if(this->vma_info.usage == VMA_MEMORY_USAGE_UNKNOWN) { this->vma_info.usage = VMA_MEMORY_USAGE_AUTO; }
    if(!(this->vma_info.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        this->vk_info.usage |= VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    }
    VK_CHECK(vmaCreateBuffer(vma, &this->vk_info, &this->vma_info, &buffer, &alloc, &alloc_info));
    if(buffer) { set_debug_name(buffer, name); }
    mapped = alloc_info.pMappedData;
    if(vk_info.usage & VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR) {
        const auto bda_info = Vks(VkBufferDeviceAddressInfo{ .buffer = buffer });
        bda = vkGetBufferDeviceAddress(dev, &bda_info);
    }
}