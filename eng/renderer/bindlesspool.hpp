#pragma once

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

// todo: remove this
#include <vulkan/vulkan.h>

namespace eng
{
namespace gfx
{

struct DescriptorSetVk
{
    u32 setidx{ ~0u };
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
    DescriptorPoolVk(u32 max_allocs, std::span<DescriptorSizeRatio> sizes)
        : max_allocs(max_allocs), sizes(sizes.begin(), sizes.end())
    {
    }

    DescriptorSetVk allocate(const DescriptorLayout& layout, u32 setidx);
    void add_page();

    u32 max_allocs;
    std::vector<DescriptorSizeRatio> sizes;
    std::vector<VkDescriptorPool> used;
    std::vector<VkDescriptorPool> free;
};

class IDescriptorSetAllocator
{
  public:
    virtual ~IDescriptorSetAllocator() = default;

    virtual void bind_resources(u32 slot, std::span<const DescriptorResource> descriptors, const PipelineLayout& layout) = 0;

    virtual u32 get_bindless(const DescriptorResource& view) { return ~0u; }

    virtual void flush(CommandBufferVk* cmd) = 0;
    // virtual void reset() = 0;
};

class DescriptorSetAllocatorBindlessVk : public IDescriptorSetAllocator
{
    using SlotAllocatorType = SlotAllocator<u32>;

    struct Slot
    {
        bool operator==(const Slot& s) const
        {
            return std::tie(view, vkptr, allocator) == std::tie(s.view, s.vkptr, s.allocator);
        }
        std::variant<BufferView, ImageView, TopAccelerationStructure> view;
        u32 value{ ~0u };
        const void* vkptr{};
        SlotAllocatorType* allocator{};
    };

  public:
    DescriptorSetAllocatorBindlessVk(const PipelineLayout& global_bindless_layout);
    ~DescriptorSetAllocatorBindlessVk() override = default;

    void bind_resources(u32 slot, std::span<const DescriptorResource> descriptors, const PipelineLayout& layout) override;

    u32 get_bindless(const DescriptorResource& view) override;

    void flush(CommandBufferVk* cmd) override;

  private:
    u32 bind_resource(const DescriptorResource& desc);
    void write_descriptor(DescriptorType type, const void* view, u32 slot);
    SlotAllocatorType& get_slot_allocator(DescriptorType type)
    {
        if(type == DescriptorType::STORAGE_BUFFER) { return storage_buffer_slots; }
        if(type == DescriptorType::STORAGE_IMAGE) { return storage_image_slots; }
        if(type == DescriptorType::SAMPLED_IMAGE) { return sampled_image_slots; }
        if(type == DescriptorType::ACCELERATION_STRUCTURE) { return tlas_slots; }
        ENG_ERROR("Invalid type");
        return storage_buffer_slots;
    }

    DescriptorPoolVk pool{};
    DescriptorSetVk set{};
    std::array<u32, PushRange::MAX_PUSH_BYTES / sizeof(u32)> push_values;
    std::vector<Range32u> push_ranges;

    std::vector<VkWriteDescriptorSet> writes;
    std::deque<VkDescriptorBufferInfo> buf_writes;
    std::deque<VkDescriptorImageInfo> img_writes;
    std::deque<VkWriteDescriptorSetAccelerationStructureKHR> tlas_writes;

    SlotAllocatorType storage_buffer_slots;
    SlotAllocatorType storage_image_slots;
    SlotAllocatorType sampled_image_slots;
    SlotAllocatorType tlas_slots;

    std::unordered_map<u32, std::vector<Slot>> image_views;
    std::unordered_map<u32, std::vector<Slot>> buffer_views;
    std::unordered_map<uintptr_t, std::vector<Slot>> tlas_views;
};

} // namespace gfx
} // namespace eng
