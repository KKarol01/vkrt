#include "./descpool.hpp"
#include <eng/renderer/vulkan_structs.hpp>
#include <assets/shaders/bindless_structures.inc>

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
    assert(pool);

    VkDescriptorSetLayoutBinding bindings[]{
        { BINDLESS_STORAGE_BUFFER_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 65536, VK_SHADER_STAGE_ALL },
        { BINDLESS_STORAGE_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 65536, VK_SHADER_STAGE_ALL },
        { BINDLESS_COMBINED_IMAGE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 65536, VK_SHADER_STAGE_ALL },
        { BINDLESS_ACCELERATION_STRUCT_BINDING, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 65536, VK_SHADER_STAGE_ALL },
    };

    const auto layout_info = Vks(VkDescriptorSetLayoutCreateInfo{
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = sizeof(bindings) / sizeof(bindings[0]),
        .pBindings = bindings,
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

uint32_t BindlessDescriptorPool::register_buffer(Handle<Buffer> buffer) {
    assert(!buffers.contains(buffer));
    buffers[buffer] = buffer_counter;
    update_bindless_resource(buffer);
    return buffer_counter++;
}

uint32_t BindlessDescriptorPool::register_image_view(VkImageView view, VkImageLayout layout, VkSampler sampler) {
    assert(!views.contains(view));
    views[view] = view_counter;
    update_bindless_resource(view, layout, sampler);
    return view_counter++;
}

uint32_t BindlessDescriptorPool::get_bindless_index(Handle<Buffer> buffer) {
    if(auto it = buffers.find(buffer); it != buffers.end()) { return it->second; }
    assert(false);
    return -1ul;
}

uint32_t BindlessDescriptorPool::get_bindless_index(VkImageView image) {
    if(auto it = views.find(image); it != views.end()) { return it->second; }
    assert(false);
    return -1ul;
}

void BindlessDescriptorPool::update_bindless_resource(Handle<Buffer> buffer) {
    if(!buffers.contains(buffer)) { return; }
    buffer_updates.push_back(Vks(VkDescriptorBufferInfo{
        .buffer = RendererVulkan::get_instance()->get_buffer(buffer).buffer, .offset = 0, .range = VK_WHOLE_SIZE }));
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
