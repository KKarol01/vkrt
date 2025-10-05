#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstdint>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)
#include <eng/common/slotallocator.hpp>
#include <eng/common/hash.hpp>

namespace eng
{
namespace gfx
{

struct Pipeline;
struct CommandBuffer;

// todo: probably wanna move it to image view
struct BufferView
{
    auto operator<=>(const BufferView&) const = default;
    Handle<Buffer> buffer;
    Range range;
};

} // namespace gfx
} // namespace eng

ENG_DEFINE_STD_HASH(eng::gfx::BufferView, eng::hash::combine_fnv1a(t.buffer, t.range.offset, t.range.size));

namespace eng
{
namespace gfx
{

class BindlessPool
{
  public:
    using index_t = uint32_t;
    static inline constexpr auto INVALID_INDEX = ~index_t{};

    BindlessPool(Handle<DescriptorPool> pool, Handle<DescriptorSet> set);
    void bind(CommandBuffer* cmd);

    uint32_t get_index(Handle<Buffer> handle, Range range = { 0, ~0ull });
    uint32_t get_index(Handle<Texture> handle);
    uint32_t get_index(Handle<Sampler> handle);
    void free_index(Handle<Buffer> handle);
    void free_index(Handle<Texture> handle);
    void update_index(Handle<Buffer> handle);
    void update_index(Handle<Texture> handle);
    void update_index(Handle<Sampler> handle);
    void update();

  private:
    Handle<DescriptorPool> pool{};
    Handle<DescriptorSet> set{};

    SlotAllocator buffer_slots;
    SlotAllocator image_slots;
    SlotAllocator texture_slots;
    SlotAllocator sampler_slots;
    std::unordered_map<Handle<Buffer>, std::vector<std::pair<BufferView, index_t>>> buffer_indices;
    std::unordered_map<Handle<Texture>, index_t> image_indices;
    std::unordered_map<Handle<Texture>, index_t> texture_indices;
    std::unordered_map<Handle<Sampler>, index_t> sampler_indices;
    std::vector<VkWriteDescriptorSet> updates;
    std::deque<VkDescriptorBufferInfo> buffer_updates;
    std::deque<VkDescriptorImageInfo> image_updates;
    std::deque<VkDescriptorImageInfo> sampler_updates;
};

} // namespace gfx
} // namespace eng
