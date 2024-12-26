#pragma once
#include <vector>
#include <string>
#include <variant>
#include <utility>
#include <span>
#include <vulkan/vulkan.h>
#include <VulkanMemoryAllocator/include/vk_mem_alloc.h>
#include "common/types.hpp"
#include "vulkan_structs.hpp"

class Buffer {
  public:
    constexpr Buffer() = default;
    Buffer(const std::string& name, size_t size, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, size_t size, uint32_t alignment, VkBufferUsageFlags usage, bool map);
    Buffer(const std::string& name, VkBufferCreateInfo create_info, VmaAllocationCreateInfo alloc_info, uint32_t alignment);

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    bool push_data(std::span<const std::byte> data, uint32_t offset);
    bool push_data(std::span<const std::byte> data) { return push_data(data, size); }
    bool push_data(const void* data, size_t size_bytes) { return push_data(data, size_bytes, size); }
    bool push_data(const void* data, size_t size_bytes, size_t offset) {
        return push_data(std::span{ static_cast<const std::byte*>(data), size_bytes }, offset);
    }
    template <typename T> bool push_data(const std::vector<T>& vec) { return push_data(vec, size); }
    template <typename T> bool push_data(const std::vector<T>& vec, uint32_t offset) {
        return push_data(std::as_bytes(std::span{ vec }), offset);
    }
    template <typename... Ts> bool push_data(uint32_t offset, const Ts&... ts) {
        std::array<std::byte, (sizeof(Ts) + ...)> arr{};
        uint64_t data_offset = 0;
        const auto pack = [&arr, &data_offset](const auto& e) {
            memcpy(&arr[data_offset], std::addressof(e), sizeof(e));
            data_offset += sizeof(e);
        };
        (pack(ts), ...);
        return push_data(std::span<const std::byte>{ arr.begin(), arr.end() }, offset);
    }

    void clear() { size = 0; }
    bool resize(size_t new_size);
    constexpr size_t get_free_space() const { return capacity - size; }
    void deallocate();

    std::string name;
    VkBufferUsageFlags usage{};
    uint64_t size{};
    uint64_t capacity{};
    uint32_t alignment{ 1 };
    VkBuffer buffer{};
    VmaAllocation alloc{};
    void* mapped{};
    VkDeviceAddress bda{};
};

class Image {
  public:
    constexpr Image() = default;
    Image(const std::string& name, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips, uint32_t layers,
          VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage = {});
    Image(const std::string& name, VkImage image, uint32_t width, uint32_t height, uint32_t depth, uint32_t mips,
          uint32_t layers, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage);
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout dst_layout);
    void transition_layout(VkCommandBuffer cmd, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                           VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access, VkImageLayout src_layout,
                           VkImageLayout dst_layout);

    void _deduce_aspect(VkImageUsageFlags usage);
    void _create_default_view(int dims, VkImageUsageFlags usage);

    VkImage image{};
    VmaAllocation alloc{};
    VkImageView view{};
    VkFormat format{};
    VkImageAspectFlags aspect{};
    VkImageLayout current_layout{ VK_IMAGE_LAYOUT_UNDEFINED };
    VkImageUsageFlags usage{};
    uint32_t width{};
    uint32_t height{};
    uint32_t depth{};
    uint32_t mips{};
    uint32_t layers{};
};

class SamplerStorage {
  public:
    VkSampler get_sampler();
    VkSampler get_sampler(VkFilter filter, VkSamplerAddressMode address);
    VkSampler get_sampler(VkSamplerCreateInfo info);

  private:
    std::vector<std::pair<VkSamplerCreateInfo, VkSampler>> samplers;
};

class ImageStatefulBarrier {
  public:
    constexpr ImageStatefulBarrier(Image& img, VkImageAspectFlags aspect, uint32_t base_mip, uint32_t mips,
                                   uint32_t base_layer, uint32_t layers, VkImageLayout start_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   VkPipelineStageFlags2 start_stage = VK_PIPELINE_STAGE_2_NONE,
                                   VkAccessFlags2 start_access = VK_ACCESS_2_NONE)
        : image{ &img }, current_range{ aspect, base_mip, mips, base_layer, layers }, current_layout{ start_layout },
          current_stage{ start_stage }, current_access{ start_access } {}

    constexpr ImageStatefulBarrier(Image& img, VkImageLayout start_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   VkPipelineStageFlags2 start_stage = VK_PIPELINE_STAGE_2_NONE,
                                   VkAccessFlags2 start_access = VK_ACCESS_2_NONE)
        : image{ &img }, current_range{ img.aspect, 0, img.mips, 0, img.layers }, current_layout{ start_layout },
          current_stage{ start_stage }, current_access{ start_access } {}

    void insert_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        insert_barrier(cmd, current_layout, dst_stage, dst_access, current_range);
    }

    void insert_barrier(VkCommandBuffer cmd, VkImageLayout dst_layout, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        insert_barrier(cmd, dst_layout, dst_stage, dst_access, current_range);
    }

  private:
    void insert_barrier(VkCommandBuffer cmd, VkImageLayout new_layout, VkPipelineStageFlags2 new_stage,
                        VkAccessFlags2 new_access, VkImageSubresourceRange new_range) {
        auto barrier = Vks(VkImageMemoryBarrier2{ .srcStageMask = current_stage,
                                                  .srcAccessMask = current_access,
                                                  .dstStageMask = new_stage,
                                                  .dstAccessMask = new_access,
                                                  .oldLayout = current_layout,
                                                  .newLayout = new_layout,
                                                  .image = image->image,
                                                  .subresourceRange = current_range });

        auto dep = Vks(VkDependencyInfo{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        });
        vkCmdPipelineBarrier2(cmd, &dep);

        current_range = new_range;
        current_layout = new_layout;
        current_stage = new_stage;
        current_access = new_access;
        image->current_layout = new_layout;
    }

    Image* image;
    VkImageSubresourceRange current_range;
    VkImageLayout current_layout;
    VkPipelineStageFlags2 current_stage;
    VkAccessFlags2 current_access;
};

struct RecordingSubmitInfo {
    std::vector<VkCommandBuffer> buffers;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> waits;
    std::vector<std::pair<VkSemaphore, VkPipelineStageFlags2>> signals;
};

// class QueueScheduler {
//   public:
//     QueueScheduler() = default;
//     QueueScheduler(VkQueue queue);
//
//     void enqueue(const RecordingSubmitInfo& info, VkFence fence = nullptr);
//     void enqueue_wait_submit(const RecordingSubmitInfo& info, VkFence fence = nullptr);
//
//   private:
//     VkQueue vkqueue;
// };

class CommandPool {
  public:
    constexpr CommandPool() noexcept = default;
    CommandPool(uint32_t queue_index, VkCommandPoolCreateFlags flags = {});
    ~CommandPool() noexcept;

    CommandPool(const CommandPool&) noexcept = delete;
    CommandPool& operator=(const CommandPool&) noexcept = delete;
    CommandPool(CommandPool&& other) noexcept;
    CommandPool& operator=(CommandPool&& other) noexcept;

    VkCommandBuffer allocate(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin(VkCommandBufferUsageFlags flags = {}, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    VkCommandBuffer begin_onetime(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    void end(VkCommandBuffer buffer);
    void reset();

    std::vector<std::pair<VkCommandBuffer, bool>> buffers;
    VkCommandPool cmdpool{};
};
