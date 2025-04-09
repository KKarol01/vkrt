#pragma once

#include <eng/renderer/common.hpp>
#include <vulkan/vulkan.h>

namespace rendergraph2 {

struct Resource;

enum class AccessFlags : uint32_t {

};

struct Access {
    Handle<Resource> resource{};
    Flags<AccessFlags> flags{};
    VkAccessFlags2 access{};
    VkPipelineStageFlags2 stage{};
    VkImageLayout dst_layout{};
};

using resource_pt = std::variant<Buffer*, Image*>;
using resource_ht = std::variant<Handle<Buffer>, Handle<Image>>;
using resource_bt = std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>;
using resource_cb_t = Callback<resource_pt()>;

class RenderPass {
  public:
    explicit RenderPass(const std::string& name);
    virtual ~RenderPass() noexcept = default;
    virtual void render(VkCommandBuffer cmd) = 0;
    std::span<const Access> get_accesses() const { return accesses; }

    std::string name;
    std::vector<Access> accesses;
};



} // namespace rendergraph2