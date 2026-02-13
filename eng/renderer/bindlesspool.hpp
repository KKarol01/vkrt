#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <span>
#include <deque>
#include <array>
#include <vector>
#include <cstdint>
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

    virtual void bind_resources(uint32_t slot, std::span<const DescriptorResource> resources) = 0;

    virtual uint32_t get_bindless(const ImageView& view, bool is_storage) { return ~0u; }
    virtual uint32_t get_bindless(const BufferView& view) { return ~0u; }

    virtual void flush(CommandBufferVk* cmd) = 0;
    // virtual void reset() = 0;
};

class DescriptorSetAllocatorBindlessVk : public IDescriptorSetAllocator
{
    struct Views
    {
        struct Slot
        {
            uint32_t slot{ ~0u };
            union {
                BufferView buffer{};
                ImageView image;
            };
            bool is_storage{}; // valid only if it's image. determines whether it is storage or sampled.
        };
        union {
            VkBuffer vkbuffer{};
            VkImage vkimage;
        };
        std::vector<Slot> slots;
    };
    // struct FreedResource
    //{
    //     SlotAllocator* allocator{};
    //     uint32_t slot;
    //     uint64_t frame{}; // frame at which the resource was freed. if delta is bigger than frames in flight, it can be overwritten
    // };

  public:
    DescriptorSetAllocatorBindlessVk(const PipelineLayout& global_bindless_layout);
    ~DescriptorSetAllocatorBindlessVk() override = default;

    void bind_resources(uint32_t slot, std::span<const DescriptorResource> resources) override;

    uint32_t get_bindless(const ImageView& view, bool is_storage) override;
    uint32_t get_bindless(const BufferView& view) override;

    void flush(CommandBufferVk* cmd) override;
    // void reset() override;

  private:
    uint32_t bind_resource(BufferView view);
    uint32_t bind_resource(ImageView view, bool is_storage);
    void write_descriptor(DescriptorType type, const void* view, uint32_t slot);

    DescriptorPoolVk pool{};
    DescriptorSetVk set{};
    std::array<uint32_t, PushRange::MAX_PUSH_BYTES / sizeof(uint32_t)> push_values;
    std::vector<Range32u> push_ranges;

    std::vector<VkWriteDescriptorSet> writes;
    std::deque<VkDescriptorBufferInfo> buf_writes;
    std::deque<VkDescriptorImageInfo> img_writes;

    SlotAllocator<uint32_t> storage_buffer_slots;
    SlotAllocator<uint32_t> storage_image_slots;
    SlotAllocator<uint32_t> sampled_image_slots;
    std::unordered_map<Handle<Buffer>, Views> buffer_views;
    std::unordered_map<Handle<Image>, Views> image_views;
    // std::vector<FreedResource> pending_frees;
};

} // namespace gfx
} // namespace eng
