#include <eng/renderer/buffer.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/set_debug_name.hpp>

Buffer::Buffer(const std::string& name, VkDevice dev, VmaAllocator vma, const VkBufferCreateInfo& info) noexcept
    : name(name), dev(dev), vma(vma), info(info) {
    if(!vma) { return; }
    VmaAllocationCreateInfo alloc_info{ .usage = VMA_MEMORY_USAGE_AUTO };
    VK_CHECK(vmaCreateBuffer(vma, &info, &alloc_info, &buffer, &alloc, nullptr));
    if(buffer) { set_debug_name(buffer, name); }
}
