#include "bindlesspool.hpp"
#include <eng/renderer/renderer.hpp>
#include <eng/renderer/submit_queue.hpp>
#include <eng/renderer/vulkan/vulkan_backend.hpp>
#include <eng/renderer/vulkan/vulkan_structs.hpp>
#include <assets/shaders/common.hlsli>
#include <eng/renderer/vulkan/to_vk.hpp>

namespace eng
{
namespace gfx
{

DescriptorSetVk DescriptorPoolVk::allocate(const DescriptorLayout& layout, u32 setidx)
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
            vec.push_back(VkDescriptorPoolSize{ .type = to_vk(sz.type), .descriptorCount = (u32)sz.ratio * max_allocs });
        }
        return vec;
    }();
    auto vk_pool_info = vk::VkDescriptorPoolCreateInfo{};
    vk_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    vk_pool_info.maxSets = max_allocs;
    vk_pool_info.poolSizeCount = (u32)sizes.size();
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

void DescriptorSetAllocatorBindlessVk::bind_resources(u32 slot, std::span<const DescriptorResource> descriptors,
                                                      const PipelineLayout& layout)
{
    slot = 0; // slot 0, because bindless requires all pipeline layouts to be the same and have one descriptor layout/descriptor table
    if(layout.layout.size() != 1)
    {
        ENG_ASSERT(false, "Invalid slot index {}", slot);
        return;
    }
    const auto& setlayout = layout.layout[slot].get();
    for(auto i = 0u; i < descriptors.size(); ++i)
    {
        const auto& desc = descriptors[i];
        if(!desc) { continue; }
        const auto binding = i;
        ENG_ASSERT(desc.index != ~0u); // if index > 0, binding should be saved and all with index > 0 be treated as one binding of an array
        u32 bindless_index = bind_resource(desc);
        push_values[binding] = bindless_index;
        push_ranges.push_back({ binding, 1 });
    }
}

void DescriptorSetAllocatorBindlessVk::on_resource_destroyed(Handle<Buffer> buffer)
{
    auto it = m_buffer_views_set.find(buffer);
    if(it != m_buffer_views_set.end())
    {
        it->second.first->erase(it->second.second);
        m_buffer_views_set.erase(it);
    }
}

void DescriptorSetAllocatorBindlessVk::on_resource_destroyed(Handle<Image> image)
{
    auto* imgmd = (ImageMetadataVk*)image->md.ptr;
    for(const auto* v : imgmd->views)
    {
        auto it = m_image_views_set.equal_range(v->view_hash);
        for(auto vit = it.first; vit != it.second; ++vit)
        {
            std::get<SlotAllocatorType*>(vit->second)->erase(std::get<u32>(vit->second));
        }
        m_image_views_set.erase(v->view_hash);
    }
}

u32 DescriptorSetAllocatorBindlessVk::get_bindless(const DescriptorResource& view) { return bind_resource(view); }

void DescriptorSetAllocatorBindlessVk::flush(CommandBufferVk* cmd)
{
    if(writes.size() > 0)
    {
        vkUpdateDescriptorSets(RendererBackendVk::get_dev(), (u32)writes.size(), writes.data(), 0, nullptr);
        writes.clear();
        buf_writes.clear();
        img_writes.clear();
    }
    for(const auto& range : push_ranges)
    {
        cmd->push_constants(ShaderStage::ALL, &push_values[range.offset],
                            { (u32)(range.offset * sizeof(u32)), (u32)(range.size * sizeof(u32)) });
    }
    push_ranges.clear();
    cmd->bind_sets(&set, 1);
}

u32 DescriptorSetAllocatorBindlessVk::bind_resource(const DescriptorResource& desc)
{
    auto& alloc = get_slot_allocator(desc.type);

    if(desc.is_buffer())
    {
        ENG_ASSERT(desc.as_buffer().buffer);
        auto it = m_buffer_views_set.find(desc.as_buffer().buffer);
        if(it != m_buffer_views_set.end()) { return it->second.second; }

        auto ret = m_buffer_views_set.emplace(desc.as_buffer().buffer, std::make_pair(&alloc, *alloc.allocate()));
        write_descriptor(desc.type, &desc.as_buffer(), ret.first->second.second);
        return ret.first->second.second;
    }
    else if(desc.is_image())
    {
        ENG_ASSERT(desc.as_image().image);
        auto* viewmd = (const ImageViewMetadataVk*)desc.as_image().get_md();
        auto eqit = m_image_views_set.equal_range(viewmd->view_hash);
        for(auto it = eqit.first; it != eqit.second; ++it)
        {
            if(std::get<DescriptorType>(it->second) == desc.type) { return std::get<u32>(it->second); }
        }

        auto it = m_image_views_set.emplace(std::piecewise_construct, std::forward_as_tuple(viewmd->view_hash),
                                            std::forward_as_tuple(desc.type, &alloc, *alloc.allocate()));
        write_descriptor(desc.type, &desc.as_image(), std::get<u32>(it->second));
        return std::get<u32>(it->second);
    }
    else if(desc.is_tlas())
    {
        auto it = m_tlas_map.find((uintptr_t)desc.as_tlas());
        if(it != m_tlas_map.end()) { return it->second; }

        auto res = m_tlas_map.emplace((uintptr_t)desc.as_tlas(), *alloc.allocate());
        write_descriptor(desc.type, &desc.as_image(), res.first->second);
        return res.first->first;
    }
    else
    {
        ENG_WARN("Unknown descriptor type {}", desc.resource.index());
        return ~0u;
    }
}

void DescriptorSetAllocatorBindlessVk::write_descriptor(DescriptorType type, const void* view, u32 slot)
{
    auto& write = writes.emplace_back(vk::VkWriteDescriptorSet{});
    write.dstSet = set.set;
    write.descriptorCount = 1;
    write.descriptorType = to_vk(type);
    write.dstArrayElement = slot;
    write.dstBinding = type == DescriptorType::STORAGE_BUFFER           ? ENG_BINDLESS_STORAGE_BUFFER_BINDING
                       : type == DescriptorType::STORAGE_IMAGE          ? ENG_BINDLESS_STORAGE_IMAGE_BINDING
                       : type == DescriptorType::SAMPLED_IMAGE          ? ENG_BINDLESS_SAMPLED_IMAGE_BINDING
                       : type == DescriptorType::ACCELERATION_STRUCTURE ? ENG_BINDLESS_ACCELERATION_STRUCT_BINDING
                                                                        : ~0u;
    assert(write.dstBinding != ~0u);

    if(type == DescriptorType::STORAGE_BUFFER)
    {
        auto* info = &buf_writes.emplace_back();
        const auto& engview = *(const BufferView*)view;
        info->buffer = engview.buffer->md.vk()->buffer;
        info->offset = engview.range.offset;
        info->range = engview.range.size;
        write.pBufferInfo = info;
    }
    else if(type == DescriptorType::STORAGE_IMAGE)
    {
        auto* info = &img_writes.emplace_back();
        const auto& engview = *(const ImageView*)view;
        info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        info->imageView = ((const ImageViewMetadataVk*)engview.get_md())->view;
        info->sampler = {};
        write.pImageInfo = info;
    }
    else if(type == DescriptorType::SAMPLED_IMAGE)
    {
        auto* info = &img_writes.emplace_back();
        const auto& engview = *(const ImageView*)view;
        info->imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        info->imageView = ((const ImageViewMetadataVk*)engview.get_md())->view;
        info->sampler = {};
        write.pImageInfo = info;
    }
    else if(type == DescriptorType::ACCELERATION_STRUCTURE)
    {
        auto* info = &tlas_writes.emplace_back();
        const auto& engview = *(const VkAccelerationStructureKHR*)view;
        info->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        info->accelerationStructureCount = 1;
        info->pAccelerationStructures = &engview;
        write.pNext = info;
    }
    else
    {
        ENG_ASSERT("Invalid type");
        writes.pop_back();
        return;
    }
}

} // namespace gfx
} // namespace eng