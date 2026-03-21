#pragma once

#ifdef ENG_DEBUG_BUILD
#include <string_view>
#include <vulkan/vulkan.h>
#include <eng/renderer/vulkan/vulkan_backend.hpp>
#endif

namespace eng
{
namespace gfx
{

// clang-format off
template<typename VkStruct> struct VkObject {};
template<> struct VkObject<VkImage> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_IMAGE; };
template<> struct VkObject<VkImageView> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_IMAGE_VIEW; };
template<> struct VkObject<VkBuffer> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_BUFFER; };
template<> struct VkObject<VkPipeline> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_PIPELINE; };
template<> struct VkObject<VkSemaphore> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_SEMAPHORE; };
template<> struct VkObject<VkFence> { inline static constexpr VkObjectType type = VK_OBJECT_TYPE_FENCE; };
// clang-format on

template <typename VkStruct> inline void set_debug_name(VkStruct object, std::string_view name)
{
#ifdef ENG_DEBUG_BUILD
    VkDebugUtilsObjectNameInfoEXT obj_name{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext = nullptr,
        .objectType = VkObject<VkStruct>::type,
        .objectHandle = reinterpret_cast<uint64_t>(object),
        .pObjectName = name.data(),
    };
    vkSetDebugUtilsObjectNameEXT(RendererBackendVk::get_dev(), &obj_name);
#endif
}

} // namespace gfx
} // namespace eng