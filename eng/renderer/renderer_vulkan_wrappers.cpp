#include <eng/renderer/renderer_vulkan_wrappers.hpp>
#include <eng/engine.hpp>
#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/set_debug_name.hpp>

// clang-format off
inline RendererVulkan& get_renderer() { return *static_cast<RendererVulkan*>(Engine::get().renderer); }
// clang-format on

VkSampler SamplerStorage::get_sampler() {
    auto sampler_info = Vks(VkSamplerCreateInfo{});
    return get_sampler(sampler_info);
}

VkSampler SamplerStorage::get_sampler(VkFilter filter, VkSamplerAddressMode address) {
    auto sampler_info = Vks(VkSamplerCreateInfo{
        .magFilter = filter,
        .minFilter = filter,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = address,
        .addressModeV = address,
        .addressModeW = address,
        .maxLod = 1.0f,
    });
    return get_sampler(sampler_info);
}

VkSampler SamplerStorage::get_sampler(ImageFilter filter, ImageAddressing address) {
    const VkFilter _filter = filter == ImageFilter::LINEAR    ? VK_FILTER_LINEAR
                             : filter == ImageFilter::NEAREST ? VK_FILTER_NEAREST
                                                              : VK_FILTER_MAX_ENUM;
    const VkSamplerAddressMode _address = address == ImageAddressing::REPEAT  ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                          : address == ImageAddressing::CLAMP ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                                                                              : VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    return get_sampler(_filter, _address);
}

VkSampler SamplerStorage::get_sampler(VkSamplerCreateInfo vk_info) {
    for(const auto& s : samplers) {
        if(s.first.magFilter == vk_info.magFilter && s.first.minFilter == vk_info.minFilter && s.first.addressModeU == vk_info.addressModeU) {
            return s.second;
        }
    }
    VkSampler sampler;
    VK_CHECK(vkCreateSampler(get_renderer().dev, &vk_info, nullptr, &sampler));
    samplers.emplace_back(vk_info, sampler);
    return sampler;
}

CommandPool::CommandPool(uint32_t queue_index, VkCommandPoolCreateFlags flags) noexcept {
    auto vk_info = Vks(VkCommandPoolCreateInfo{
        .flags = flags,
        .queueFamilyIndex = queue_index,
    });
    VK_CHECK(vkCreateCommandPool(get_renderer().dev, &vk_info, {}, &cmdpool));
}

CommandPool::~CommandPool() noexcept {
    if(cmdpool) { vkDestroyCommandPool(get_renderer().dev, cmdpool, nullptr); }
}

CommandPool::CommandPool(CommandPool&& other) noexcept { *this = std::move(other); }

CommandPool& CommandPool::operator=(CommandPool&& other) noexcept {
    cmdpool = std::exchange(other.cmdpool, nullptr);
    return *this;
}

VkCommandBuffer CommandPool::allocate(VkCommandBufferLevel level) {
    if(free.empty()) {
        auto vk_info = Vks(VkCommandBufferAllocateInfo{
            .commandPool = cmdpool,
            .level = level,
            .commandBufferCount = 1,
        });
        VkCommandBuffer buffer;
        VK_CHECK(vkAllocateCommandBuffers(get_renderer().dev, &vk_info, &buffer));
        used.push_back(buffer);
    } else {
        used.push_back(free.front());
        free.pop_front();
    }
    return used.back();
}

VkCommandBuffer CommandPool::begin(VkCommandBufferUsageFlags flags, VkCommandBufferLevel level) {
    auto vk_info = Vks(VkCommandBufferBeginInfo{
        .flags = flags,
    });
    VkCommandBuffer buffer = allocate(level);
    VK_CHECK(vkBeginCommandBuffer(buffer, &vk_info));
    return buffer;
}

VkCommandBuffer CommandPool::begin_onetime(VkCommandBufferLevel level) {
    return begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void CommandPool::end(VkCommandBuffer buffer) { VK_CHECK(vkEndCommandBuffer(buffer)); }

void CommandPool::reset() {
    vkResetCommandPool(get_renderer().dev, cmdpool, {});
    free = std::move(used);
}
