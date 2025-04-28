#pragma once

#include <eng/renderer/common.hpp>
#include <eng/renderer/passes/passes.hpp>

namespace gfx {

enum class ResourceFlags : uint32_t {
    PER_FRAME_BIT = 0x1,
};
ENG_ENABLE_FLAGS_OPERATORS(ResourceFlags);

static constexpr auto swapchain_handle = Handle<Image>{};
using resource_ht = std::variant<Handle<Buffer>, Handle<Image>>;
using resource_bt = std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>;
using resource_cb_t = std::function<resource_ht()>;

struct Resource {
    bool is_buffer() const { return resource.index() == 0; }
    bool is_image() const { return resource.index() == 1; }
    resource_ht resource;
    Flags<ResourceFlags> flags{};
    resource_cb_t resource_cb;
};

class RenderPass;

struct RenderStage {
    std::vector<RenderPass*> passes;
    std::vector<VkBufferMemoryBarrier2> buffer_barriers;
    std::vector<VkImageMemoryBarrier2> image_barriers;
};

class RenderGraph {
  public:
    Handle<Resource> make_resource(resource_cb_t res_cb, ResourceFlags flags = {});
    template <typename T, typename... Args> void add_pass(Args&&... args) {
        passes.push_back(std::make_unique<T>(std::forward<Args>(args)...));
    }
    void clear_passes() {
        passes.clear();
        stages.clear();
    }
    void bake();
    void render(VkCommandBuffer cmd);
    static Buffer& unpack_buffer(resource_ht handle);
    static Image& unpack_image(resource_ht handle);

    std::unordered_map<Handle<Resource>, Resource> resources;
    std::unordered_map<resource_ht, Handle<Resource>> resource_handles;
    std::vector<std::unique_ptr<RenderPass>> passes;
    std::vector<RenderStage> stages;
};

} // namespace gfx