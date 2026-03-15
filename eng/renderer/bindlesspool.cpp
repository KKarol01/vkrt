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

        const auto vk_alloc_info = Vks(VkDescriptorSetAllocateInfo{
            .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &layout.md.vk->layout });
        VkDescriptorSet vkset;
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
    const auto vk_pool_info = Vks(VkDescriptorPoolCreateInfo{ .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                                              .maxSets = max_allocs,
                                                              .poolSizeCount = (uint32_t)sizes.size(),
                                                              .pPoolSizes = vksizes.data() });
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
    slot.view = view;

    auto eqit = slots.equal_range(slot);
    bool resource_changed = false;
    for(const auto& s : std::ranges::subrange(eqit.first, eqit.second))
    {
        const auto vkptr = view.is_buffer() ? view.as_buffer().buffer->md.ptr : view.as_image().image->md.ptr;
        if((s.view.is_buffer() && s.vkbuffer != vkptr) || (!s.view.is_buffer() && s.vkimage != vkptr))
        {
            resource_changed = true;
            get_slot_allocator(s.view.type).erase(SlotAllocatorType::Slot{ s.slot });
        }
        if(!resource_changed && s.view == view) { return s.slot; }
    }
    if(resource_changed) { slots.erase(eqit.first, eqit.second); }
    slot.slot = *get_slot_allocator(view.type).allocate();
    if(view.is_buffer()) { slot.vkbuffer = view.as_buffer().buffer->md.vk()->buffer; }
    else { slot.vkimage = view.as_image().image->md.vk()->image; }
    write_descriptor(view.type, view.is_buffer() ? (void*)&view.as_buffer() : (void*)&view.as_image(), slot.slot);
    return slots.emplace(slot)->slot;
}

void DescriptorSetAllocatorBindlessVk::write_descriptor(DescriptorType type, const void* view, uint32_t slot)
{
    auto& write = writes.emplace_back(Vks(VkWriteDescriptorSet{}));
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