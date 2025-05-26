#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <variant>
#include <deque>
#include <vector>
#include <cstdint>
#include <atomic>
#include <eng/renderer/renderer_vulkan.hpp> // required in the header, cause buffer/handle<buffer> only causes linktime error on MSVC (clang links)

namespace gfx
{

class BindlessPool
{
  public:
    BindlessPool() noexcept = default;
    BindlessPool(VkDevice dev) noexcept;

    void bind(VkCommandBuffer cmd, VkPipelineBindPoint point);

    VkDescriptorSetLayout get_set_layout() const { return set_layout; }
    VkPipelineLayout get_pipeline_layout() const { return pipeline_layout; }

    uint32_t get_index(Handle<Buffer> buffer);
    uint32_t get_index(Handle<Texture> texture);
    uint32_t unregister_index(Handle<Buffer> buffer);
    uint32_t unregister_index(Handle<Texture> texture);

  private:
    void update();

    VkDevice dev{};
    VkDescriptorPool pool{};
    VkDescriptorSetLayout set_layout{};
    VkPipelineLayout pipeline_layout{};
    VkDescriptorSet set{};

    std::atomic<uint32_t> buffer_counter{};
    std::atomic<uint32_t> view_counter{};
    std::unordered_map<Handle<Buffer>, uint32_t> buffers;
    std::unordered_map<Handle<Texture>, uint32_t> textures;
    std::deque<uint32_t> free_buffer_handles;
    std::deque<uint32_t> free_image_handles;
    std::vector<VkWriteDescriptorSet> updates;
    std::deque<VkDescriptorBufferInfo> buffer_updates;
    std::deque<VkDescriptorImageInfo> image_updates;
};

} // namespace gfx