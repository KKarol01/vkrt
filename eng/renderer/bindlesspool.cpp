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
        uint32_t bindless_index;
        switch(res.type)
        {
        case DescriptorType::STORAGE_BUFFER:
        {
            bindless_index = bind_resource(res.buffer_view);
            break;
        }
        case DescriptorType::STORAGE_IMAGE:
        case DescriptorType::SAMPLED_IMAGE:
        {
            bindless_index = bind_resource(res.image_view, res.type == DescriptorType::STORAGE_IMAGE);
            break;
        }
        default:
        {
            ENG_ASSERT("Unhandle case");
            continue;
        }
        }
        push_values[res.binding] = bindless_index;
        push_ranges.push_back({ res.binding, 1 });
    }
}

uint32_t DescriptorSetAllocatorBindlessVk::get_bindless(const ImageView& view, bool is_storage)
{
    return bind_resource(view, is_storage);
}

uint32_t DescriptorSetAllocatorBindlessVk::get_bindless(const BufferView& view) { return bind_resource(view); }

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

// void DescriptorSetAllocatorBindlessVk::reset()
//{
//     const auto& r = get_renderer();
//     auto delcount = 0u;
//     for(auto i = 0u; i < pending_frees.size(); ++i)
//     {
//         auto& pf = pending_frees[i];
//         if(r.frame_index - pf.frame < r.frames_in_flight)
//         {
//             // break, as next frees were only added later, so cannot be freed too.
//             break;
//         }
//         pf.allocator->free_slot(pf.slot);
//         ++delcount;
//     }
//     pending_frees.erase(pending_frees.begin(), pending_frees.begin() + delcount);
//     push_ranges.clear();
// }

uint32_t DescriptorSetAllocatorBindlessVk::bind_resource(BufferView view)
{
    const auto& buf = view.buffer.get();
    auto [it, success] = buffer_views.emplace(view.buffer, Views{ .vkbuffer = buf.md.as_vk()->buffer });
    auto& views = buffer_views[view.buffer];
    if(views.vkbuffer != buf.md.as_vk()->buffer)
    {
        views.vkbuffer = buf.md.as_vk()->buffer;
        for(const auto& e : views.slots)
        {
            storage_buffer_slots.erase(SlotAllocatorType::Slot{ e.slot });
        }
        views.slots.clear();
    }

    const auto vit =
        std::find_if(views.slots.begin(), views.slots.end(), [view](const auto& slot) { return slot.buffer == view; });

    if(vit != views.slots.end()) { return vit->slot; }
    const auto alloc = (uint32_t)*storage_buffer_slots.allocate();
    views.slots.push_back(Views::Slot{ .slot = alloc, .buffer = view, .is_storage = true });
    write_descriptor(DescriptorType::STORAGE_BUFFER, &view, alloc);
    return alloc;
}

uint32_t DescriptorSetAllocatorBindlessVk::bind_resource(ImageView view, bool is_storage)
{
    const auto& img = view.image.get();
    auto [it, success] = image_views.emplace(view.image, Views{ .vkimage = img.md.as_vk()->image });
    auto& views = it->second;

    // Assuming how destroy_resource works in the renderer, The another resource may obtain the same handled only after period of two full frames,
    // therefore it is safe to free the slots immediately here, instead of putting the slots into pending frees and waiting another two frames.
    if(views.vkimage != img.md.as_vk()->image)
    {
        views.vkimage = img.md.as_vk()->image;
        for(const auto& e : views.slots)
        {
            if(e.is_storage)
            {
                storage_image_slots.erase(SlotAllocatorType::Slot{ e.slot });
                // pending_frees.push_back(FreedResource{ &storage_image_slots, e.slot, get_renderer().frame_index });
            }
            else
            {
                sampled_image_slots.erase(SlotAllocatorType::Slot{ e.slot });
                // pending_frees.push_back(FreedResource{ &sampled_image_slots, e.slot, get_renderer().frame_index });
            }
        }
        views.slots.clear();
    }

    const auto vit =
        std::find_if(views.slots.begin(), views.slots.end(), [view](const auto& slot) { return slot.image == view; });

    if(vit != views.slots.end()) { return vit->slot; }
    const auto alloc = (uint32_t)*storage_buffer_slots.allocate();
    views.slots.push_back(Views::Slot{ .slot = alloc, .image = view, .is_storage = is_storage });
    write_descriptor(is_storage ? DescriptorType::STORAGE_BUFFER : DescriptorType::SAMPLED_IMAGE, &view, alloc);
    return alloc;
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
        info->buffer = engview.buffer->md.as_vk()->buffer;
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