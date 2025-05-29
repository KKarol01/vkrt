#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstdint>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)

namespace gfx
{

class BindlessPool
{
  public:
    using index_t = uint32_t;

    BindlessPool() noexcept = default;
    BindlessPool(VkDevice dev) noexcept;

    void bind(VkCommandBuffer cmd, VkPipelineBindPoint point);

    VkDescriptorSetLayout get_set_layout() const { return set_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }

    index_t get_index(Handle<Buffer> buffer);
    index_t get_index(Handle<Texture> texture);
    void rem_index(Handle<Buffer> buffer);
    void rem_index(Handle<Texture> texture);
    void update_index(index_t index, VkBuffer buffer);
    void update_index(index_t index, VkImageView view, VkImageLayout layout, VkSampler sampler = nullptr);

  private:
    void update();

    VkDevice dev{};
    VkDescriptorPool pool{};
    VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};
    VkDescriptorSet set{};

    index_t buffer_counter{};
    index_t texture_counter{};
    std::unordered_map<Handle<Buffer>, index_t> buffers;
    std::unordered_map<Handle<Texture>, index_t> textures;
    std::deque<index_t> free_buff_idxs;
    std::deque<index_t> free_img_idxs;
    std::vector<VkWriteDescriptorSet> updates;
    std::deque<VkDescriptorBufferInfo> buffer_updates;
    std::deque<VkDescriptorImageInfo> texture_updates;
};

} // namespace gfx