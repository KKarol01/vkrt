#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <eng/handle.hpp>

class BindlessDescriptorPool {
  public:
    constexpr BindlessDescriptorPool() noexcept = default;
    BindlessDescriptorPool(VkDevice dev) noexcept;



  private:
    VkDevice dev{};
    VkDescriptorPool pool{};
};