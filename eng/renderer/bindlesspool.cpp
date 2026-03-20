#include "bindlesspool.hpp"
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/vulkan_structs.hpp>
#include <assets/shaders/bindless_structures.glsli>
#include <eng/common/to_vk.hpp>

namespace eng
{
namespace gfx
{

DescriptorSetVk DescriptorPoolVk::allocate(const DescriptorLayout& layout, uint32_t setidx)
{
    while(true)
    {
        VkDescriptorPool pool;
        if(free.empty()) { add_page(); }
        pool = free.back();

        auto vk_alloc_info = vk::VkDescriptorSetAllocateInfo{};
        vk_alloc_info.descriptorPool = pool;
        vk_alloc_info.descriptorSetCount = 1;
        vk_alloc_info.pSetLayouts = &layout.md.vk->layout;

        VkDescriptorSet vkset{};
        const auto res = vkAllocateDescriptorSets(RendererBackendVk::get_dev(), &vk_alloc_info, &vkset);
        if(res != VK_SUCCESS)
        {
            if(res != VK_ERROR_OUT_OF_POOL_MEMORY)
            {
                ENG_ERROR("Unhandled error");
                return {};
            }
            used.push_back(pool);
            free.pop_back();
            continue;
        }
        return { setidx, vkset };
    }
}

void DescriptorPoolVk::add_page()
{
    const auto vksizes = [this]() {
        std::vector<VkDescriptorPoolSize> vec;
        vec.reserve(sizes.size());
        for(const auto& sz : sizes)
        {
            vec.push_back(VkDescriptorPoolSize{ .type = to_vk(sz.type), .descriptorCount = (uint32_t)sz.ratio * max_allocs });
        }
        return vec;
    }();
    auto vk_pool_info = vk::VkDescriptorPoolCreateInfo{};
    vk_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    vk_pool_info.maxSets = max_allocs;
    vk_pool_info.poolSizeCount = (uint32_t)sizes.size();
    vk_pool_info.pPoolSizes = vksizes.data();
    VK_CHECK(vkCreateDescriptorPool(RendererBackendVk::get_dev(), &vk_pool_info, nullptr, &free.emplace_back()));
    max_allocs = std::min(4096.0, std::ceil(max_allocs * 1.5));
}

DescriptorSetAllocatorBindlessVk::DescriptorSetAllocatorBindlessVk(const PipelineLayout& global_bindless_layout)
{
    const auto& layout0 = global_bindless_layout.layout[0].get();
    std::vector<DescriptorSizeRatio> ratios = [&layout0] {
        std::vector<DescriptorSizeRatio> ratios;
        ratios.reserve(layout0.layout.size());
        for(const auto& d : layout0.layout)
        {
            ratios.push_back(DescriptorSizeRatio{ d.type, (float)d.size });
        }
        return ratios;
    }();
    DescriptorPoolVk pool{ 1, ratios };

    set = pool.allocate(layout0, 0);
}

void DescriptorSetAllocatorBindlessVk::bind_set(uint32_t slot, std::span<const DescriptorResource> resources,
                                                const PipelineLayout& layout)
{
    slot = 0; // slot 0, because bindless requires all pipeline layouts to be the same and have one descriptor layout/descriptor table
    if(layout.layout.size() != 1)
    {
        ENG_ASSERT(false, "Invalid slot index {}", slot);
        return;
    }
    const auto& setlayout = layout.layout[slot].get();
    for(auto i = 0u; i < resources.size(); ++i)
    {
        const auto& res = resources[i];
        ENG_ASSERT(res.binding != ~0u && res.index != ~0u);
        uint32_t bindless_index = bind_resource(res.view);
        push_values[res.binding] = bindless_index;
        push_ranges.push_back({ res.binding, 1 });
    }
}

uint32_t DescriptorSetAllocatorBindlessVk::get_bindless(const DescriptorResourceView& view)
{
    return bind_resource(view);
}

void DescriptorSetAllocatorBindlessVk::flush(CommandBufferVk* cmd)
{
    if(writes.size() > 0)
    {
        vkUpdateDescriptorSets(RendererBackendVk::get_dev(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        writes.clear();
        buf_writes.clear();
        img_writes.clear();
    }
    for(const auto& range : push_ranges)
    {
        cmd->push_constants(ShaderStage::ALL, &push_values[range.offset],
                            { (uint32_t)(range.offset * sizeof(uint32_t)), (uint32_t)(range.size * sizeof(uint32_t)) });
    }
    push_ranges.clear();
    cmd->bind_sets(&set, 1);
}

uint32_t DescriptorSetAllocatorBindlessVk::bind_resource(const DescriptorResourceView& view)
{
    Slot slot{};
    slot.allocator = &get_slot_allocator(view.type);

    std::vector<Slot>* found_slots{};

    if(view.is_buffer())
    {
        slot.view = view.as_buffer();
        slot.vkptr = view.as_buffer().buffer->md.vk()->buffer;
        auto it = buffer_views.find(*view.as_buffer().buffer);
        if(it != buffer_views.end()) { found_slots = &it->second; }
    }
    else
    {
        slot.view = view.as_image();
        slot.vkptr = view.as_image().image->md.vk()->image;
        auto it = image_views.find(*view.as_image().image);
        if(it != image_views.end()) { found_slots = &it->second; }
    }

    if(found_slots && found_slots->size())
    {
        if((*found_slots)[0].vkptr != slot.vkptr)
        {
            for(auto& s : *found_slots)
            {
                s.allocator->erase(SlotAllocatorType::Slot{ s.value });
            }
            found_slots->clear();
        }
        else
        {
            auto it = std::find(found_slots->begin(), found_slots->end(), slot);
            if(it != found_slots->end()) { return it->value; }
        }
    }

    slot.value = *slot.allocator->allocate();
    write_descriptor(view.type, view.is_buffer() ? (void*)&view.as_buffer() : (void*)&view.as_image(), slot.value);
    if(view.is_buffer()) { buffer_views[*view.as_buffer().buffer].push_back(slot); }
    else { image_views[*view.as_image().image].push_back(slot); }
    return slot.value;
}

void DescriptorSetAllocatorBindlessVk::write_descriptor(DescriptorType type, const void* view, uint32_t slot)
{
    auto& write = writes.emplace_back(vk::VkWriteDescriptorSet{});
    write.dstSet = set.set;
    write.descriptorCount = 1;
    write.descriptorType = to_vk(type);
    write.dstArrayElement = slot;
    write.dstBinding = type == DescriptorType::STORAGE_BUFFER  ? ENG_BINDLESS_STORAGE_BUFFER_BINDING
                       : type == DescriptorType::STORAGE_IMAGE ? ENG_BINDLESS_STORAGE_IMAGE_BINDING
                       : type == DescriptorType::SAMPLED_IMAGE ? ENG_BINDLESS_SAMPLED_IMAGE_BINDING
                                                               : ~0u;
    assert(write.dstBinding != ~0u);

    if(type == DescriptorType::STORAGE_BUFFER)
    {
        auto* info = &buf_writes.emplace_back();
        const auto& engview = *(BufferView*)view;
        info->buffer = engview.buffer->md.vk()->buffer;
        info->offset = engview.range.offset;
        info->range = engview.range.size;
        write.pBufferInfo = info;
    }
    else if(type == DescriptorType::STORAGE_IMAGE)
    {
        auto* info = &img_writes.emplace_back();
        const auto& engview = (const ImageView*)view;
        info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        info->imageView = engview->get_md().vk->view;
        info->sampler = {};
        write.pImageInfo = info;
    }
    else if(type == DescriptorType::SAMPLED_IMAGE)
    {
        auto* info = &img_writes.emplace_back();
        const auto& engview = (const ImageView*)view;
        info->imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        info->imageView = engview->get_md().vk->view;
        info->sampler = {};
        write.pImageInfo = info;
    }
    else
    {
        ENG_ERROR("Invalid type");
        writes.pop_back();
        return;
    }
}

} // namespace gfx
} // namespace eng