#pragma once

#include <eng/renderer/common.hpp>
#include <eng/renderer/renderer_vulkan.hpp>

namespace rendergraph2 {

enum class ResourceFlags : uint32_t {

};

using resource_pt = std::variant<Buffer*, Image*>;
using resource_ht = std::variant<Handle<Buffer>, Handle<Image>>;
using resource_bt = std::variant<VkBufferMemoryBarrier2, VkImageMemoryBarrier2>;
using resource_cb_t = Callback<resource_pt()>;

struct Resource {
    resource_bt barrier;
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

    std::unordered_map<Handle<Resource>, Resource> resources;
    std::vector<std::unique_ptr<RenderPass>> passes;
    std::vector<RenderStage> stages;
};

} // namespace rendergraph2