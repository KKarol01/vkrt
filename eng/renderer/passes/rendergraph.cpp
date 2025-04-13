#include "rendergraph.hpp"
#include <eng/renderer/passes/passes.hpp>
#include <eng/renderer/renderer_vulkan.hpp>

namespace rendergraph2 {

Handle<Resource> RenderGraph::make_resource(resource_cb_t res_cb, ResourceFlags flags) {
    if(auto it = resource_handles.find(res_cb()); it != resource_handles.end()) { return it->second; }
    const auto handle = Handle<Resource>{ generate_handle };
    resources[handle] = Resource{ .resource = res_cb(), .flags = flags, .resource_cb = res_cb };
    resource_handles[res_cb()] = handle;
    return handle;
}

void RenderGraph::bake() {
    struct ResourceAccessHistory {
        int first_read{ INT32_MAX };
        int first_write{ INT32_MAX };
        int last_read{ -1 };
        int last_write{ -1 };
        int first_index{ INT32_MAX };
        int last_index{ -1 };
        Access first_access{};
        Access last_access{};
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
        if(acc.flags == AccessFlags::READ_WRITE_BIT || acc.flags == AccessFlags::NONE_BIT) {
            return std::max(hist.last_read, hist.last_write) + 1;
        } else {
            return std::max(hist.last_write,
                            (hist.last_read > -1 && hist.last_access.layout != acc.layout) ? hist.last_read : hist.last_write) +
                   1;
        }
    };
    const auto update_access_history = [this, &history, &get_stage](const Access& acc, int stage) {
        const auto& res = resources.at(acc.resource);
        const auto is_buffer = res.resource.index() == 0;
        auto& hist = history[acc.resource];
        if(acc.flags & AccessFlags::READ_BIT) {
            hist.first_read = std::min(hist.first_read, stage);
            hist.last_read = std::max(hist.last_read, stage);
        }
        if(acc.flags & AccessFlags::WRITE_BIT) {
            hist.first_write = std::min(hist.first_write, stage);
            hist.last_write = std::max(hist.last_write, stage);
        }
        if(hist.first_index == INT32_MAX) {
            hist.first_index = is_buffer ? get_stage(stage).buffer_barriers.size() : get_stage(stage).image_barriers.size();
            hist.first_access = acc;
        }
        hist.last_index = is_buffer ? get_stage(stage).buffer_barriers.size() : get_stage(stage).image_barriers.size();
        hist.last_access = acc;
    };
    const auto generate_barrier = [this, &history](const Access& acc) -> resource_bt {
        const auto& resource = resources.at(acc.resource);
        if(auto* bres = std::get_if<Handle<Buffer>>(&resource.resource)) {
            return Vks(VkBufferMemoryBarrier2{
                .srcStageMask = history[acc.resource].last_access.stage,
                .srcAccessMask = history[acc.resource].last_access.access,
                .dstStageMask = acc.stage,
                .dstAccessMask = acc.access,
                .size = VK_WHOLE_SIZE,
            });
        } else if(auto* ires = std::get_if<Handle<Image>>(&resource.resource)) {
            const auto handle = resource.resource_cb();
            const auto& img = unpack_image(handle);
            const auto layout = (acc.flags & AccessFlags::FROM_UNDEFINED_LAYOUT_BIT) ? VK_IMAGE_LAYOUT_UNDEFINED
                                                                                     : history[acc.resource].last_access.layout;
            return Vks(VkImageMemoryBarrier2{ .srcStageMask = history[acc.resource].last_access.stage,
                                              .srcAccessMask = history[acc.resource].last_access.access,
                                              .dstStageMask = acc.stage,
                                              .dstAccessMask = acc.access,
                                              .oldLayout = layout,
                                              .newLayout = acc.layout,
                                              .subresourceRange = {
                                                  .aspectMask = img.deduce_aspect(),
                                                  .levelCount = VK_REMAINING_MIP_LEVELS,
                                                  .layerCount = VK_REMAINING_ARRAY_LAYERS,
                                              } });
        } else {
            ENG_ERROR("Invalid resource type: {}", resource.resource.index());
            return VkBufferMemoryBarrier2{};
        }
    };

    for(const auto& pass : passes) {
        const auto stage_idx = std::transform_reduce(
            pass->accesses.begin(), pass->accesses.end(), 0, [](auto a, auto b) { return std::max(a, b); },
            [&get_acc_stage_idx](const auto& acc) { return get_acc_stage_idx(acc); });
        auto& stage = get_stage(stage_idx);
        stage.passes.push_back(&*pass);
        for(const auto& e : pass->accesses) {
            const auto barrier = generate_barrier(e);
            update_access_history(e, stage_idx);
            if(const auto* bbar = std::get_if<VkBufferMemoryBarrier2>(&barrier)) {
                stage.buffer_barriers.push_back(*bbar);
            } else if(const auto* ibar = std::get_if<VkImageMemoryBarrier2>(&barrier)) {
                stage.image_barriers.push_back(*ibar);
            } else {
                ENG_ERROR("Invalid barrier type");
            }
        }
    }

    for(const auto& [res, hist] : history) {
        auto& r = resources.at(res);
        auto& fs = get_stage(std::min(hist.first_read, hist.first_write));
        auto& ls = get_stage(std::max(hist.last_read, hist.last_write));
        if(std::holds_alternative<Handle<Buffer>>(r.resource)) {
            auto& fb = fs.buffer_barriers.at(hist.first_index);
            auto& lb = fs.buffer_barriers.at(hist.last_index);
            if(!r.flags.test(ResourceFlags::PER_FRAME_BIT)) {
                fb.srcStageMask = lb.dstStageMask;
                fb.srcAccessMask = lb.dstAccessMask;
            }
        } else if(std::holds_alternative<Handle<Image>>(r.resource)) {
            auto& fb = fs.image_barriers.at(hist.first_index);
            auto& lb = ls.image_barriers.at(hist.last_index);
            if(!r.flags.test(ResourceFlags::PER_FRAME_BIT)) {
                fb.srcStageMask = lb.dstStageMask;
                fb.srcAccessMask = lb.dstAccessMask;
            }
            if(!hist.first_access.flags.test(AccessFlags::FROM_UNDEFINED_LAYOUT_BIT)) { fb.oldLayout = lb.newLayout; }
        } else {
            ENG_ERROR("Invalid resource type");
        }
    }
}

void RenderGraph::render(VkCommandBuffer cmd) {
    for(auto& s : stages) {
        auto bit = s.buffer_barriers.begin();
        auto iit = s.image_barriers.begin();
        for(const auto& p : s.passes) {
            for(const auto& a : p->accesses) {
                const auto r = resources.at(a.resource);
                if(r.is_buffer()) {
                    (bit++)->buffer = unpack_buffer(r.resource_cb()).buffer;
                } else if(r.is_image()) {
                    auto& img = unpack_image(r.resource_cb());
                    if(iit->oldLayout != VK_IMAGE_LAYOUT_UNDEFINED) { iit->oldLayout = img.current_layout; }
                    iit->image = img.image;
                    img.current_layout = iit->newLayout;
                    ++iit;
                }
            }
        }

        auto dep_info = Vks(VkDependencyInfo{
            .bufferMemoryBarrierCount = static_cast<uint32_t>(s.buffer_barriers.size()),
            .pBufferMemoryBarriers = s.buffer_barriers.data(),
            .imageMemoryBarrierCount = static_cast<uint32_t>(s.image_barriers.size()),
            .pImageMemoryBarriers = s.image_barriers.data(),
        });
        vkCmdPipelineBarrier2(cmd, &dep_info);
        for(auto p : s.passes) {
            auto& pass = *p;
            if(pass.pipeline) { vkCmdBindPipeline(cmd, pass.pipeline->bind_point, pass.pipeline->pipeline); }
            pass.render(cmd);
        }
    }
}

Buffer& RenderGraph::unpack_buffer(resource_ht handle) {
    return RendererVulkan::get_buffer(std::get<Handle<Buffer>>(handle));
}

Image& RenderGraph::unpack_image(resource_ht handle) {
    auto img = std::get<Handle<Image>>(handle);
    if(img == swapchain_handle) { return RendererVulkan::get_instance()->swapchain.get_current_image(); }
    return RendererVulkan::get_image(img);
}

} // namespace rendergraph2
