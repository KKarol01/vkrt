#pragma once

#ifndef NDEBUG
#include <string>
#include <vulkan/vulkan.h>
#include <eng/renderer/renderer_vulkan.hpp>
#endif

// clang-format off
template<typename VkStruct> struct VkObject {};
template<> struct VkObject<VkImage> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_IMAGE; };
template<> struct VkObject<VkImageView> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_IMAGE_VIEW; };
template<> struct VkObject<VkBuffer> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_BUFFER; };
template<> struct VkObject<VkPipeline> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_PIPELINE; };
// clang-format on

template <typename VkStruct> inline void set_debug_name(VkStruct object, const std::string& name) {
#ifndef NDEBUG
    VkDebugUtilsObjectNameInfoEXT obj_name{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VkObject<VkStruct>::type,
        .objectHandle = reinterpret_cast<uint64_t>(object),
        .pObjectName = name.c_str(),
    };
    vkSetDebugUtilsObjectNameEXT(((gfx::RendererVulkan*)Engine::get().renderer)->dev, &obj_name);
#endif
}
