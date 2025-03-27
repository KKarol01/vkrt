#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <unordered_multimap>
#include <eng/handle.hpp>

class Buffer;
class Image;

class BindlessDescriptorPool {
    struct BindlessImage {
        VkImageLayout layout{};
        VkSampler sampler{};
        uint32_t index{ -1ul };
    };

  public:
    BindlessDescriptorPool() noexcept = default;
    BindlessDescriptorPool(VkDevice dev) noexcept;

    uint32_t get_bindless_index(Handle<Buffer> buffer);
    uint32_t get_bindless_index(VkImageView image, VkImageLayout layout, VkSampler sampler);

  private:
    VkDevice dev{};
    VkDescriptorPool pool{};
    uint32_t buffer_counter{};
    uint32_t image_counter{};
    std::unordered_map<Handle<Buffer>, uint32_t> buffers;
    std::unordered_multimap<VkImageView, BindlessImage> images;
};