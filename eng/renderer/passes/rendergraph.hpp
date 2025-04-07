#pragma once

#include <eng/renderer/passes/common.hpp>

namespace rendergraph {

class RenderPass;

class RenderGraph {
    struct Stage {
        std::vector<RenderPass*> passes;
        std::vector<Handle<Resource>> buffer_resources;
        std::vector<Handle<Resource>> image_resources;
        std::vector<VkImageMemoryBarrier2> image_barriers;
        std::vector<VkBufferMemoryBarrier2> buffer_barriers;
    };

  public:
    Handle<Resource> get_resource(const Resource& resource);
    template <typename T> RenderGraph& add_pass() {
        passes.push_back(std::make_unique<T>(this));
        return *this;
    }
    void bake();
    void render(VkCommandBuffer cmd);
    void clear_passes();

  private:
    Resource& get_resource(Handle<Resource> handle);

    std::unordered_map<Handle<Resource>, Resource> resources;
    std::vector<std::unique_ptr<RenderPass>> passes;
    std::vector<Stage> stages;
};
} // namespace rendergraph