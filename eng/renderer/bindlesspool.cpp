#include "./bindlesspool.hpp"
#include <eng/renderer/vulkan_structs.hpp>
#include <assets/shaders/bindless_structures.glsli>
#include <eng/renderer/submit_queue.hpp>
#include <eng/common/to_vk.hpp>

namespace eng
{
namespace gfx
{

BindlessPool::BindlessPool(Handle<DescriptorPool> pool, Handle<DescriptorSet> set) : pool(pool), set(set)
{
    auto* r = RendererBackendVulkan::get_instance();
    assert(set);
}

void BindlessPool::bind(CommandBuffer* cmd)
{
    update();
    cmd->bind_descriptors(&pool.get(), &pool->get_dset(set), { 0, 1 });
}

uint32_t BindlessPool::get_index(Handle<Buffer> handle, Range range)
{
    auto& vec = buffer_indices[handle];
    BufferView bv{ handle, range };
    auto vecit = std::find_if(vec.begin(), vec.end(), [&bv](auto& e) { return e.first == bv; });
    if(vecit == vec.end())
    {
        vec.emplace_back(bv, buffer_slots.allocate_slot());
        update_index(handle);
        vecit = vec.begin() + (vec.size() - 1);
    }
    return vecit->second;
}

uint32_t BindlessPool::get_index(Handle<Texture> handle)
{
    const auto has_sampler = (bool)(handle->sampler);
    const auto ret = has_sampler ? texture_indices.emplace(handle, ~index_t{}) : image_indices.emplace(handle, ~index_t{});
    if(ret.second)
    {
        if(has_sampler) { ret.first->second = texture_slots.allocate_slot(); }
        else { ret.first->second = image_slots.allocate_slot(); }
        update_index(handle);
    }
    return ret.first->second;
}

uint32_t BindlessPool::get_index(Handle<Sampler> handle)
{
    const auto ret = sampler_indices.emplace(handle, ~index_t{});
    if(ret.second)
    {
        ret.first->second = sampler_slots.allocate_slot();
        update_index(handle);
    }
    return ret.first->second;
}

void BindlessPool::free_index(Handle<Buffer> handle)
{
    if(auto it = buffer_indices.find(handle); it != buffer_indices.end())
    {
        for(const auto& e : it->second)
        {
            buffer_slots.free_slot(e.second);
        }
        buffer_indices.erase(it);
    }
}

void BindlessPool::free_index(Handle<Texture> handle)
{
    const auto has_sampler = (bool)(handle->sampler);
    if(has_sampler)
    {
        if(auto it = texture_indices.find(handle); it != texture_indices.end())
        {
            texture_slots.free_slot(it->second);
            texture_indices.erase(it);
        }
    }
    else
    {
        if(auto it = image_indices.find(handle); it != image_indices.end())
        {
            image_slots.free_slot(it->second);
            image_indices.erase(it);
        }
    }
}

void BindlessPool::update_index(Handle<Buffer> handle)
{
    if(!buffer_indices.contains(handle)) { return; }
    const auto& views = buffer_indices.at(handle);

    for(auto& e : views)
    {
        const auto& update = buffer_updates.emplace_back(Vks(VkDescriptorBufferInfo{
            .buffer = VkBufferMetadata::get(handle.get()).buffer, .offset = e.first.range.offset, .range = e.first.range.size }));
        const auto write = Vks(VkWriteDescriptorSet{ .dstSet = VkDescriptorSetMetadata::get(pool->get_dset(set))->set,
                                                     .dstBinding = BINDLESS_STORAGE_BUFFER_BINDING,
                                                     .dstArrayElement = e.second,
                                                     .descriptorCount = 1,
                                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                     .pBufferInfo = &update });
        updates.push_back(write);
    }
}

void BindlessPool::update_index(Handle<Texture> handle)
{
    const auto& txt = handle.get();
    const auto index = txt.sampler ? texture_indices.at(handle) : image_indices.at(handle);
    const auto& update = image_updates.emplace_back(Vks(VkDescriptorImageInfo{
        .sampler = txt.sampler ? VkSamplerMetadata::get(txt.sampler.get()).sampler : nullptr,
        .imageView = VkImageViewMetadata::get(txt.view.get()).view,
        .imageLayout = to_vk(txt.layout) }));
    const auto write = Vks(VkWriteDescriptorSet{
        .dstSet = VkDescriptorSetMetadata::get(pool->get_dset(set))->set,
        .dstBinding = (uint32_t)(txt.sampler ? BINDLESS_SAMPLED_IMAGE_BINDING : BINDLESS_STORAGE_IMAGE_BINDING),
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = (txt.sampler ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        .pImageInfo = &update });
    updates.push_back(write);
}

void BindlessPool::update_index(Handle<Sampler> handle)
{
    const auto& res = handle.get();
    const auto index = sampler_indices.at(handle);
    const auto& update =
        sampler_updates.emplace_back(Vks(VkDescriptorImageInfo{ .sampler = VkSamplerMetadata::get(res).sampler }));
    const auto write = Vks(VkWriteDescriptorSet{ .dstSet = VkDescriptorSetMetadata::get(pool->get_dset(set))->set,
                                                 .dstBinding = (uint32_t)BINDLESS_SAMPLER_BINDING,
                                                 .dstArrayElement = index,
                                                 .descriptorCount = 1,
                                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                                 .pImageInfo = &update });
    updates.push_back(write);
}

void BindlessPool::update()
{
    if(updates.empty()) { return; }
    vkUpdateDescriptorSets(RendererBackendVulkan::get_instance()->dev, updates.size(), updates.data(), 0, nullptr);
    updates.clear();
    image_updates.clear();
    buffer_updates.clear();
    sampler_updates.clear();
}

} // namespace gfx
} // namespace eng