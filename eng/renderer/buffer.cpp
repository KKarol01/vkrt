#include <eng/renderer/buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

Buffer::Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, const VkBufferCreateInfo& info,
               const VmaAllocationCreateInfo& alloc_info) noexcept
    : name(name), dev(dev), vma(vma), info(info) {
    if(!dev || !vma) { return; }
    VmaAllocationInfo alloc_res_info;
    VK_CHECK(vmaCreateBuffer(vma, &info, &alloc_info, &buffer, &alloc, &alloc_res_info));
    if(buffer) { set_debug_name(buffer, name); }
    mapped = alloc_res_info.pMappedData;
    if(info.usage & VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR) {
        const auto bda_info = Vks(VkBufferDeviceAddressInfo{ .buffer = buffer });
        bda = vkGetBufferDeviceAddress(dev, &bda_info);
    }
}
