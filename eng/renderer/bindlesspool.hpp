#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstdint>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)
#include <eng/common/slotallocator.hpp>

namespace gfx
{

class BindlessPool
{
  public:
    using bindless_index_t = uint32_t;
    static inline constexpr auto INVALID_INDEX = ~bindless_index_t{};

    BindlessPool() noexcept = default;
    BindlessPool(VkDevice dev) noexcept;

    void bind(VkCommandBuffer cmd, VkPipelineBindPoint point);

    VkDescriptorSetLayout get_set_layout() const { return set_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }

    uint32_t get_index(Handle<Buffer> handle);
    uint32_t get_index(Handle<Texture> handle);
    void free_index(Handle<Buffer> handle);
    void free_index(Handle<Texture> handle);
    void update_index(Handle<Buffer> handle);
    void update_index(Handle<Texture> handle);

  private:
    void update();

    VkDevice dev{};
    VkDescriptorPool pool{};
    VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};
    VkDescriptorSet set{};

    SlotAllocator buffer_slots;
    SlotAllocator texture_slots;
    std::unordered_map<Handle<Buffer>, bindless_index_t> buffer_indices;
    std::unordered_map<Handle<Texture>, bindless_index_t> texture_indices;
    std::vector<VkWriteDescriptorSet> updates;
    std::deque<VkDescriptorBufferInfo> buffer_updates;
    std::deque<VkDescriptorImageInfo> texture_updates;
};

} // namespace gfx