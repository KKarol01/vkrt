#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <span>
#include <deque>
#include <array>
#include <vector>
#include <cstdint>
#include <set>
#include <eng/common/handle.hpp>
#include <eng/renderer/renderer.hpp>
#include <eng/common/slotallocator.hpp>
#include <eng/common/hash.hpp>

namespace eng
{
namespace gfx
{

struct DescriptorSetVk
{
    uint32_t setidx{ ~0u };
    VkDescriptorSet set{};
};

struct DescriptorSizeRatio
{
    DescriptorType type;
    float ratio{ 1.0f };
};

class DescriptorPoolVk
{
  public:
    DescriptorPoolVk() = default;
    DescriptorPoolVk(uint32_t max_allocs, std::span<DescriptorSizeRatio> sizes)
        : max_allocs(max_allocs), sizes(sizes.begin(), sizes.end())
    {
    }

    DescriptorSetVk allocate(const DescriptorLayout& layout, uint32_t setidx);
    void add_page();

    uint32_t max_allocs;
    std::vector<DescriptorSizeRatio> sizes;
    std::vector<VkDescriptorPool> used;
    std::vector<VkDescriptorPool> free;
};

class IDescriptorSetAllocator
{
  public:
    virtual ~IDescriptorSetAllocator() = default;

    virtual void bind_set(uint32_t slot, std::span<const DescriptorResource> resources, const PipelineLayout& layout) = 0;

    virtual uint32_t get_bindless(const DescriptorResourceView& view) { return ~0u; }

    virtual void flush(CommandBufferVk* cmd) = 0;
    // virtual void reset() = 0;
};

class DescriptorSetAllocatorBindlessVk : public IDescriptorSetAllocator
{
    using SlotAllocatorType = SlotAllocator<uint32_t>;

    struct Slot
    {
        bool operator==(const Slot& s) const
        {
            return std::tie(view, vkptr, allocator) == std::tie(s.view, s.vkptr, s.allocator);
        }
        std::variant<BufferView, ImageView> view;
        uint32_t value{ ~0u };
        const void* vkptr{};
        SlotAllocatorType* allocator{};
    };

  public:
    DescriptorSetAllocatorBindlessVk(const PipelineLayout& global_bindless_layout);
    ~DescriptorSetAllocatorBindlessVk() override = default;

    void bind_set(uint32_t slot, std::span<const DescriptorResource> resources, const PipelineLayout& layout) override;

    uint32_t get_bindless(const DescriptorResourceView& view) override;

    void flush(CommandBufferVk* cmd) override;

  private:
    uint32_t bind_resource(const DescriptorResourceView& view);
    void write_descriptor(DescriptorType type, const void* view, uint32_t slot);
    SlotAllocatorType& get_slot_allocator(DescriptorType type)
    {
        if(type == DescriptorType::STORAGE_BUFFER) { return storage_buffer_slots; }
        if(type == DescriptorType::STORAGE_IMAGE) { return storage_image_slots; }
        if(type == DescriptorType::SAMPLED_IMAGE) { return sampled_image_slots; }
        ENG_ERROR("Invalid type");
        return storage_buffer_slots;
    }

    DescriptorPoolVk pool{};
    DescriptorSetVk set{};
    std::array<uint32_t, PushRange::MAX_PUSH_BYTES / sizeof(uint32_t)> push_values;
    std::vector<Range32u> push_ranges;

    std::vector<VkWriteDescriptorSet> writes;
    std::deque<VkDescriptorBufferInfo> buf_writes;
    std::deque<VkDescriptorImageInfo> img_writes;

    SlotAllocatorType storage_buffer_slots;
    SlotAllocatorType storage_image_slots;
    SlotAllocatorType sampled_image_slots;

    std::unordered_map<uint32_t, std::vector<Slot>> image_views;
    std::unordered_map<uint32_t, std::vector<Slot>> buffer_views;
};

} // namespace gfx
} // namespace eng
