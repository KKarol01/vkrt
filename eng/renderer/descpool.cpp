#include "./descpool.hpp"
#include <eng/renderer/vulkan_structs.hpp>
#include <assets/shaders/bindless_structures.inc.glsl>

namespace gfx {

BindlessDescriptorPool::BindlessDescriptorPool(VkDevice dev) noexcept : dev(dev) {
    if(!dev) { return; }
    std::vector<VkDescriptorPoolSize> sizes{ { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256 },
                                             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 },
                                             { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256 } };
    if(RendererVulkan::get_instance()->supports_raytracing) {
        sizes.push_back({ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 16 });
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
        { BINDLESS_STORAGE_BUFFER_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 65536, VK_SHADER_STAGE_ALL },
        { BINDLESS_STORAGE_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 65536, VK_SHADER_STAGE_ALL },
        { BINDLESS_COMBINED_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536, VK_SHADER_STAGE_ALL }
    };

    const auto binding_flag = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                              VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    std::vector<VkDescriptorBindingFlags> binding_flags{ binding_flag, binding_flag, binding_flag };
    if(RendererVulkan::get_instance()->supports_raytracing) {
        bindings.push_back({ BINDLESS_ACCELERATION_STRUCT_BINDING, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 65536,
                             VK_SHADER_STAGE_ALL });
        binding_flags.push_back(binding_flag);
    }
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

void BindlessDescriptorPool::bind(VkCommandBuffer cmd, VkPipelineBindPoint point) {
    update();
    vkCmdBindDescriptorSets(cmd, point, pipeline_layout, 0, 1, &set, 0, nullptr);
}

uint32_t BindlessDescriptorPool::get_bindless_index(Handle<Buffer> buffer) {
    if(!buffer) {
        ENG_WARN("buffer is null");
        return ~0ull;
    }
    if(auto it = buffers.find(buffer); it != buffers.end()) { return it->second; }
    buffers[buffer] = buffer_counter;
    update_bindless_resource(buffer);
    return buffer_counter++;
}

uint32_t BindlessDescriptorPool::get_bindless_index(Handle<Texture> texture) {
    if(!texture) {
        ENG_WARN("view is null");
        return ~0ull;
    }
    if(auto it = textures.find(texture); it != textures.end()) { return it->second; }
    textures[texture] = view_counter;
    const auto& tex = RendererVulkan::get_instance()->textures.at(texture);
    update_bindless_resource(tex.view, tex.layout, tex.sampler);
    return view_counter++;
}

void BindlessDescriptorPool::update_bindless_resource(Handle<Buffer> buffer) {
    if(!buffers.contains(buffer)) { return; }
    buffer_updates.push_back(Vks(VkDescriptorBufferInfo{
        .buffer = RendererVulkan::get_buffer(buffer).buffer, .offset = 0, .range = VK_WHOLE_SIZE }));
    updates.push_back(Vks(VkWriteDescriptorSet{ .dstSet = set,
                                                .dstBinding = BINDLESS_STORAGE_BUFFER_BINDING,
                                                .dstArrayElement = buffers.at(buffer),
                                                .descriptorCount = 1,
                                                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                .pBufferInfo = &buffer_updates.back() }));
}

void BindlessDescriptorPool::update_bindless_resource(VkImageView view, VkImageLayout layout, VkSampler sampler) {
    image_updates.push_back(Vks(VkDescriptorImageInfo{ .sampler = sampler, .imageView = view, .imageLayout = layout }));
    updates.push_back(Vks(VkWriteDescriptorSet{
        .dstSet = set,
        .dstBinding = (uint32_t)(sampler ? BINDLESS_COMBINED_IMAGE_BINDING : BINDLESS_STORAGE_IMAGE_BINDING),
        .dstArrayElement = view_counter,
        .descriptorCount = 1,
        .descriptorType = sampler ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo = &image_updates.back() }));
}

void BindlessDescriptorPool::update() {
    if(updates.empty()) { return; }
    vkUpdateDescriptorSets(dev, updates.size(), updates.data(), 0, nullptr);
    updates.clear();
    image_updates.clear();
    buffer_updates.clear();
}

} // namespace gfx