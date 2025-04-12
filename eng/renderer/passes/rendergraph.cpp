#include "rendergraph.hpp"
#include <eng/renderer/passes/passes.hpp>

namespace rendergraph2 {

Handle<Resource> RenderGraph::make_resource(resource_ht res_template, resource_cb_t res_cb) {
    if(auto it = resource_handles.find(res_template); it != resource_handles.end()) { return it->second; }
    const auto handle = Handle<Resource>{ generate_handle };
    resources[handle] = Resource{ .resource_cb = res_cb };
    resource_handles[res_template] = handle;
    return handle;
}

void RenderGraph::bake() {
    struct ResourceAccessHistory {
        int first_read{ INT32_MAX };
        int first_write{ INT32_MAX };
        int last_read{ -1 };
        int last_write{ -1 };
        VkPipelineStageFlags2 dst_stage{};
        VkAccessFlags2 dst_access{};
        VkImageLayout dst_layout{};
    };
    std::unordered_map<Handle<Resource>, ResourceAccessHistory> history;
    stages.clear();
    stages.reserve(passes.size());
    const auto get_stage = [this](uint32_t index) -> auto& {
        if(stages.size() <= index) { stages.resize(index + 1); }
        return stages.at(index);
    };
    const auto get_acc_stage_idx = [&history](const Access& acc) {
        const auto& hist = history[acc.resource];
        if(acc.type == AccessFlags::READ_WRITE_BIT || acc.type == AccessFlags::NONE_BIT) {
            return std::max(hist.last_read, hist.last_write) + 1;
        } else {
            return std::max(hist.last_write,
                            (hist.last_read > -1 && hist.dst_layout != acc.dst_layout) ? hist.last_read : hist.last_write) +
                   1;
        }
    };
    const auto update_access_history = [&history](const Access& acc, int stage) {
        auto& hist = history[acc.resource];
        if(acc.type & AccessFlags::READ_BIT) {
            hist.first_read = std::min(hist.first_read, stage);
            hist.last_read = std::max(hist.last_read, stage);
        }
        if(acc.type & AccessFlags::WRITE_BIT) {
            hist.first_write = std::min(hist.first_write, stage);
            hist.last_write = std::max(hist.last_write, stage);
        }
        hist.dst_layout = acc.dst_layout;
    };

    for(const auto& pass : passes) {
        const auto stage_idx = std::transform_reduce(
            pass->accesses.begin(), pass->accesses.end(), 0, [](auto a, auto b) { return std::max(a, b); },
            [&get_acc_stage_idx](const auto& acc) { return get_acc_stage_idx(acc); });
        auto& stage = get_stage(stage_idx);
        for(const auto& e : pass->accesses) {
            const auto barrier = generate_barrier(resources.at(e.resource).resource_cb());
            if(const auto* bbar = std::get_if<VkBufferMemoryBarrier2>(&barrier)) {
                stage.buffer_barriers.push_back(*bbar);
            } else if(const auto* ibar = std::get_if<VkImageMemoryBarrier2>(&barrier)) {
                stage.image_barriers.push_back(*ibar);
            } else {
                ENG_ERROR("Invalid barrier type");
            }
            update_access_history(e, stage_idx);
        }
    }
}

resource_bt RenderGraph::generate_barrier(const Access& access) const {
    const auto resource = resources.at(access.resource).resource_cb();
    if(auto* bres = std::get_if<Buffer*>(&resource)) {
        return Vks(VkBufferMemoryBarrier2{ .size = VK_WHOLE_SIZE
        ,
        .
        });
    } else if(auto* ires = std::get_if<Image*>(&resource)) {
        return Vks(VkImageMemoryBarrier2{ .subresourceRange = {
                                              .aspectMask = ires->deduce_aspect(),
                                              .levelCount = VK_REMAINING_MIP_LEVELS,
                                              .layerCount = VK_REMAINING_ARRAY_LAYERS,
                                          } });
    } else {
        ENG_ERROR("Invalid resource type: {}", resource.index());
        return VkBufferMemoryBarrier2{};
    }
}

} // namespace rendergraph2
