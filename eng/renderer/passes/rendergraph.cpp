#include "rendergraph.hpp"

namespace rendergraph2 {

Handle<Resource> RenderGraph::make_resource(resource_ht res_template, resource_cb_t res_cb) {
    if(auto it = resource_handles.find(res_template); it != resource_handles.end()) { return it->second; }
    const auto handle = Handle<Resource>{ generate_handle };
    resources[handle] = Resource{ .barrier = generate_barrier(res_template), .resource_cb = res_cb };
    resource_handles[res_template] = handle;
    return handle;
}

void RenderGraph::bake() { assert(false); }

resource_bt RenderGraph::generate_barrier(resource_ht resource) const { assert(false); }

} // namespace rendergraph2
