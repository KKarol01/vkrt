#include <vulkan/vulkan.h>
#include "engine.hpp"

// clang-format off
template<typename VkStruct> struct VkObject {};
template<> struct VkObject<VkImage> {inline static constexpr VkObjectType type = VK_OBJECT_TYPE_IMAGE; };
template<> struct VkObject<VkImageView> {inline static constexpr VkObjectType type = VK_OBJECT_TYPE_IMAGE_VIEW; };
template<> struct VkObject<VkBuffer> {inline static constexpr VkObjectType type = VK_OBJECT_TYPE_BUFFER; };
// clang-format on

template <typename VkStruct> inline void set_debug_name(VkStruct object, const std::string& name) {
#ifndef NDEBUG
    VkDebugUtilsObjectNameInfoEXT obj_name{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VkObject<VkStruct>::type,
        .objectHandle = reinterpret_cast<uint64_t>(object),
        .pObjectName = name.c_str(),
    };
    vkSetDebugUtilsObjectNameEXT(static_cast<RendererVulkan*>(Engine::renderer())->dev, &obj_name);
#endif
}
