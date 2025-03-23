#include "descpool.hpp"
#include "vulkan_structs.hpp"

BindlessDescriptorPool::BindlessDescriptorPool(VkDevice dev) noexcept : dev(dev) {
    if(!dev) { return; }
    const VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256 },
                                           { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
                                           { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256 },
                                           { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 16 } };
    const auto pool_info = Vks(VkDescriptorPoolCreateInfo{
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = sizeof(sizes) / sizeof(sizes[0]),
        .pPoolSizes = sizes,
    });
    VK_CHECK(vkCreateDescriptorPool(dev, &pool_info, {}, &pool));
}
