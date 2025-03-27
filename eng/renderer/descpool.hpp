#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <variant>
#include <deque>
#include <vector>
#include <cstdint>
#include <eng/handle.hpp>

class Buffer;
class Image;

class BindlessDescriptorPool {
  public:
    BindlessDescriptorPool() noexcept = default;
    BindlessDescriptorPool(VkDevice dev) noexcept;

    void bind();

    uint32_t register_buffer(Handle<Buffer> buffer);
    uint32_t register_image_view(VkImageView view, VkImageLayout layout, VkSampler sampler);

    uint32_t get_bindless_index(Handle<Buffer> buffer);
    uint32_t get_bindless_index(VkImageView view);

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