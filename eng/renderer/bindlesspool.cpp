#include "./bindlesspool.hpp"
#include <eng/renderer/vulkan_structs.hpp>
#include <eng/renderer/resources/resources.hpp>
#include <assets/shaders/bindless_structures.inc.glsl>

namespace gfx
{

BindlessPool::BindlessPool(VkDevice dev) noexcept : dev(dev)
{
    if(!dev) { return; }
    const auto MAX_COMBINED_IMAGES = 1024u;
    const auto MAX_STORAGE_IMAGES = 1024u;
    const auto MAX_STORAGE_BUFFERS = 1024u;
    const auto MAX_SAMPLED_IMAGES = 1024u;
    const auto MAX_SAMPLERS = 128u;
    const auto MAX_AS = 16u;
    const auto MAX_SETS = 2u;
    std::vector<VkDescriptorPoolSize> sizes{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SETS * MAX_COMBINED_IMAGES },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_SETS * MAX_STORAGE_IMAGES },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_SETS * MAX_STORAGE_BUFFERS },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_SETS * MAX_SAMPLED_IMAGES },
        { VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SETS * MAX_SAMPLERS },
    };
    if(RendererVulkan::get_instance()->supports_raytracing)
    {
        sizes.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, MAX_AS });
    }
    const auto pool_info = Vks(VkDescriptorPoolCreateInfo{
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = (uint32_t)sizes.size(),
        .pPoolSizes = sizes.data(),
    });
    VK_CHECK(vkCreateDescriptorPool(dev, &pool_info, {}, &pool));
    assert(pool);

    std::vector<VkDescriptorSetLayoutBinding> bindings{
        { BINDLESS_STORAGE_BUFFER_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_STORAGE_BUFFERS, VK_SHADER_STAGE_ALL },
        { BINDLESS_STORAGE_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_STORAGE_IMAGES, VK_SHADER_STAGE_ALL },
        { BINDLESS_COMBINED_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_COMBINED_IMAGES, VK_SHADER_STAGE_ALL },
        { BINDLESS_SAMPLED_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_SAMPLED_IMAGES, VK_SHADER_STAGE_ALL },
        { BINDLESS_SAMPLER_BINDING, VK_DESCRIPTOR_TYPE_SAMPLER, MAX_SAMPLERS, VK_SHADER_STAGE_ALL },
    };

    const auto binding_flag = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                              VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    if(RendererVulkan::get_instance()->supports_raytracing)
    {
        bindings.push_back({ BINDLESS_ACCELERATION_STRUCT_BINDING, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                             MAX_SETS * MAX_AS, VK_SHADER_STAGE_ALL });
    }
    std::vector<VkDescriptorBindingFlags> binding_flags(bindings.size(), binding_flag);
    const auto binding_flags_info = Vks(VkDescriptorSetLayoutBindingFlagsCreateInfo{
        .bindingCount = (uint32_t)binding_flags.size(),
        .pBindingFlags = binding_flags.data(),
    });

    const auto layout_info = Vks(VkDescriptorSetLayoutCreateInfo{
        .pNext = &binding_flags_info,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = (uint32_t)bindings.size(),
        .pBindings = bindings.data(),
    });
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &layout_info, nullptr, &set_layout));
    assert(set_layout);

    VkPushConstantRange pc_range{ VK_SHADER_STAGE_ALL, 0, 128 };
    const auto vk_info = Vks(VkPipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pc_range,
    });
    VK_CHECK(vkCreatePipelineLayout(dev, &vk_info, nullptr, &pipeline_layout));
    assert(pipeline_layout);

    const auto alloc_info =
        Vks(VkDescriptorSetAllocateInfo{ .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &set_layout });
    VK_CHECK(vkAllocateDescriptorSets(dev, &alloc_info, &set));
    assert(set);
}

void BindlessPool::bind(VkCommandBuffer cmd, VkPipelineBindPoint point)
{
    update();
    vkCmdBindDescriptorSets(cmd, point, pipeline_layout, 0, 1, &set, 0, nullptr);
}

uint32_t BindlessPool::get_index(Handle<Buffer> handle)
{
    const auto ret = buffer_indices.emplace(handle, buffer_slots.allocate_slot());
    if(ret.second) { update_index(handle); }
    return ret.first->second;
}

uint32_t BindlessPool::get_index(Handle<Texture> handle)
{
    const auto ret = texture_indices.emplace(handle, texture_slots.allocate_slot());
    if(ret.second) { update_index(handle); }
    return ret.first->second;
}

void BindlessPool::free_index(Handle<Buffer> handle)
{
    if(auto it = buffer_indices.find(handle); it != buffer_indices.end())
    {
        buffer_slots.free_slot(it->second);
        buffer_indices.erase(it);
    }
}

void BindlessPool::free_index(Handle<Texture> handle)
{
    if(auto it = texture_indices.find(handle); it != texture_indices.end())
    {
        texture_slots.free_slot(it->second);
        texture_indices.erase(it);
    }
}

void BindlessPool::update_index(Handle<Buffer> handle)
{
    if(!buffer_indices.contains(handle)) { return; }
    const auto index = buffer_indices.at(handle);
    const auto& update =
        buffer_updates.emplace_back(Vks(VkDescriptorBufferInfo{ .buffer = handle->buffer, .range = VK_WHOLE_SIZE }));
    const auto write = Vks(VkWriteDescriptorSet{ .dstSet = set,
                                                 .dstBinding = BINDLESS_STORAGE_BUFFER_BINDING,
                                                 .dstArrayElement = index,
                                                 .descriptorCount = 1,
                                                 .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                 .pBufferInfo = &update });
    updates.push_back(write);
}

void BindlessPool::update_index(Handle<Texture> handle)
{
    const auto index = texture_indices.at(handle);
    const auto& txt = handle.get();
    const auto& update =
        texture_updates.emplace_back(Vks(VkDescriptorImageInfo{ .sampler = reinterpret_cast<VkSampler>(*txt.sampler),
                                                                .imageView = reinterpret_cast<VkImageView>(*txt.view),
                                                                .imageLayout = static_cast<VkImageLayout>(*txt.layout) }));
    const auto write = Vks(VkWriteDescriptorSet{
        .dstSet = set,
        .dstBinding = (uint32_t)(txt.sampler ? BINDLESS_COMBINED_IMAGE_BINDING : BINDLESS_STORAGE_IMAGE_BINDING),
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = (txt.sampler ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        .pImageInfo = &update });
    updates.push_back(write);
}

void BindlessPool::update()
{
    if(updates.empty()) { return; }
    vkUpdateDescriptorSets(dev, updates.size(), updates.data(), 0, nullptr);
    updates.clear();
    texture_updates.clear();
    buffer_updates.clear();
}

} // namespace gfx