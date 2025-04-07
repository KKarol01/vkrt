#pragma once

#include <eng/renderer/pipelines.hpp>
#include <vulkan/vulkan.h>
#include <eng/common/flags.hpp>
#include <eng/handle.hpp>
#include <string>
#include <variant>
#include <vector>
#include <span>
#include <array>
#include <cstdint>
#include <deque>
#include <forward_list>
#include <filesystem>
#include <initializer_list>
#include <eng/handle.hpp>
#include <eng/renderer/buffer.hpp>
#include <eng/renderer/image.hpp>

namespace rendergraph {

constexpr uint32_t swapchain_index = ~0ul;

enum class AccessType { NONE_BIT = 0x0, READ_BIT = 0x1, WRITE_BIT = 0x2, READ_WRITE_BIT = 0x3 };
// enum class ResourceType {
//     STORAGE_IMAGE = 0x1,
//     COMBINED_IMAGE = 0x2,
//     COLOR_ATTACHMENT = 0x4,
//     ANY_IMAGE = 0x8 - 1,
//     STORAGE_BUFFER = 0x8,
//     ACCELERATION_STRUCTURE = 0x10,
// };
enum class AccessFlags : uint32_t { FROM_UNDEFINED_LAYOUT_BIT = 0x1 };
enum class ResourceFlags : uint32_t { SWAPCHAIN_IMAGE_BIT = 0x1 };

ENABLE_FLAGS_OPERATORS(AccessFlags)
ENABLE_FLAGS_OPERATORS(ResourceFlags)

struct Resource {
    Resource() noexcept = default;
    Resource(std::initializer_list<Handle<Buffer>> buffers, Flags<ResourceFlags> flags = {})
        : resource(std::deque<Handle<Buffer>>{ buffers }), flags(flags) {}
    Resource(std::initializer_list<Handle<Image>> buffers, Flags<ResourceFlags> flags = {})
        : resource(std::deque<Handle<Image>>{ buffers }), flags(flags) {}
    bool operator==(const Resource& o) const noexcept { return resource == o.resource; }
    bool is_buffer() const { return resource.index() == 0; }
    bool is_image() const { return resource.index() == 1; }
    uint32_t get_resource_count() const {
        if(is_buffer()) {
            return std::get<0>(resource).size();
        } else {
            return std::get<1>(resource).size();
        }
    }
    Handle<Buffer> get_buffer() const {
        assert(is_buffer());
        return std::get<0>(resource).front();
    }
    Handle<Image> get_image() const {
        assert(is_image());
        return std::get<1>(resource).front();
    }
    void advance() {
        if(is_buffer()) {
            std::get<0>(resource).push_back(get_buffer());
            std::get<0>(resource).pop_front();
        } else {
            std::get<1>(resource).push_back(get_image());
            std::get<1>(resource).pop_front();
        }
    }
    std::variant<std::deque<Handle<Buffer>>, std::deque<Handle<Image>>> resource; // support for double buffering and other uses where each frame the resource will be different
    Flags<ResourceFlags> flags{};
};

struct Access {
    Handle<Resource> resource{};
    Flags<AccessFlags> flags{};
    Flags<AccessType> type{};
    VkPipelineStageFlags2 stage{};
    VkAccessFlags2 access{};
    VkImageLayout layout{ VK_IMAGE_LAYOUT_MAX_ENUM };
};

template <typename T> struct SwappableResource {
    const T& get() const { return data.front(); }
    T get_swap() {
        const auto t = get();
        swap();
        return t;
    }
    void swap() {
        data.push_back(get());
        data.pop_front();
    }
    std::deque<T> data;
};

class RenderPass {
  public:
    RenderPass(const std::string& name, const std::vector<Access>& accesses, const PipelineSettings& pipeline_settings) noexcept;
    virtual ~RenderPass() noexcept = default;
    virtual void render(VkCommandBuffer cmd) = 0;
    // void make_pipeline(const PipelineSettings& settings);
    //  std::span<const std::byte> get_push_constants() const { return std::span{ push_constants }; }
    // std::span<const Access> get_accesses() const { return std::span{ accesses }; }

    std::string name;
    std::vector<Access> accesses;
    Pipeline* pipeline{};
};

} // namespace rendergraph