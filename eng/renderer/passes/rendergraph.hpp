#pragma once

#include <eng/renderer/common.hpp>
#include <eng/renderer/renderer_vulkan.hpp>

namespace rendergraph2 {

using resource_pt = std::variant<Buffer*, Image*>;
using resource_ht = std::variant<Handle<Buffer>, Handle<Image>>;
using resource_bt = std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>;
using resource_cb_t = Callback<resource_pt()>;

struct Resource {
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
    Handle<Resource> make_resource(resource_ht res_template, resource_cb_t res_cb);
    template <typename T> void add_pass(const T& t) { passes.push_back(std::make_unique<T>(t)); }
    void clear_passes() { passes.clear(); }
    void bake();

    resource_bt generate_barrier(const Access& access) const;

    std::unordered_map<Handle<Resource>, Resource> resources;
    std::unordered_map<resource_ht, Handle<Resource>> resource_handles;
    std::vector<std::unique_ptr<RenderPass>> passes;
    std::vector<RenderStage> stages;
};

} // namespace rendergraph2