#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstdint>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)
#include <eng/common/slotvec.hpp>

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

    bindless_index_t allocate_buffer_index();
    bindless_index_t allocate_texture_index();
    void free_buffer_index(bindless_index_t slot);
    void free_texture_index(bindless_index_t slot);
    void update_index(bindless_index_t index, VkBuffer buffer);
    void update_index(bindless_index_t index, VkImageView view, VkImageLayout layout, VkSampler sampler);

  private:
    void update();

    VkDevice dev{};
    VkDescriptorPool pool{};
    VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};
    VkDescriptorSet set{};

    SlotVec<bindless_index_t> buffer_slots;
    SlotVec<bindless_index_t> texture_slots;
    std::vector<VkWriteDescriptorSet> updates;
    std::deque<VkDescriptorBufferInfo> buffer_updates;
    std::deque<VkDescriptorImageInfo> texture_updates;
};

} // namespace gfx