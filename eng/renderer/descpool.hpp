#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <variant>
#include <deque>
#include <vector>
#include <cstdint>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)

class BindlessDescriptorPool {
  public:
    BindlessDescriptorPool() noexcept = default;
    BindlessDescriptorPool(VkDevice dev) noexcept;

    void bind(VkCommandBuffer cmd, VkPipelineBindPoint point);

    VkDescriptorSetLayout get_set_layout() const { return set_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }

    uint32_t get_bindless_index(Handle<Buffer> buffer);
    uint32_t get_bindless_index(VkImageView view, VkImageLayout layout, VkSampler sampler);

    void update_bindless_resource(Handle<Buffer> buffer);
    void update_bindless_resource(VkImageView view, VkImageLayout layout, VkSampler sampler);

  private:
    void update();

    VkDevice dev{};
    VkDescriptorPool pool{};
    VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};
    VkDescriptorSet set{};

    uint32_t buffer_counter{};
    uint32_t view_counter{};
    std::unordered_map<Handle<Buffer>, uint32_t> buffers;
    std::unordered_map<VkImageView, uint32_t> views;
    std::vector<VkWriteDescriptorSet> updates;
    std::deque<VkDescriptorBufferInfo> buffer_updates;
    std::deque<VkDescriptorImageInfo> image_updates;
};