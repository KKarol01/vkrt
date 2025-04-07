#include <eng/renderer/renderer_vulkan.hpp>
#include <eng/renderer/passes/renderpass.hpp>
#include "rendergraph.hpp"

using namespace rendergraph;

Handle<Resource> RenderGraph::get_resource(const Resource& resource) {
    // searching and caching added so that renderpasses don't have to get resource handles
    // from the outside code - essentialy making the user to create them ahead of time and
    // pass them around individually to each renderpass.
    for(const auto& [h, r] : resources) {
        if(r == resource) { return h; }
    }
    const auto handle = Handle<Resource>{ generate_handle };
    resources[handle] = resource;
    return handle;
}

void RenderGraph::bake() {
    auto& r = *RendererVulkan::get_instance();

    struct Accesses {
        int32_t first_read{ INT32_MAX };
        int32_t first_write{ INT32_MAX };
        int32_t last_read{ -1 };
        int32_t last_write{ -1 };
        std::vector<Access*> accesses;
    };
    std::map<Handle<Resource>, Accesses> accesses; // should be Resource::resource, because if one pass uses {img} and the other {img, img1},
                                                   // they are considered different resources and there will be no synchronization
                                                   // between them, even if they share one resource (img).
    const auto get_stage = [this](uint32_t idx) -> decltype(auto) {
        if(stages.size() <= idx) { stages.resize(idx + 1); }
        return stages.at(idx);
    };

    stages.clear();
    stages.reserve(passes.size());

    //// TODO: Maybe multithread this later (shaders for now are all precompiled)
    // for(auto& p : passes) {
    //     if(!p.pipeline) { create_pipeline(p); }
    // }

    // access history for each resource
    for(auto i = 0u; i < passes.size(); ++i) {
        auto& p = *passes.at(i);
        uint32_t stage_idx = 0;
        Stage stage;

        // 1. find resource used in the latest stage in the graph render list and make the stage after that it's
        // pass's stage (pass can only happen after the latest stage modifying one of it's resources)
        // 2. generate appropiate barriers
        // 3. append to access history
        for(auto& paccess : p.accesses) {
            auto& acc = accesses[paccess.resource];
            int32_t a_stage = 0;
            if((paccess.type & AccessType::WRITE_BIT) || (paccess.type == AccessType::NONE_BIT)) {
                a_stage = std::max(acc.last_read, acc.last_write) + 1;
            } else if(paccess.type & AccessType::READ_BIT) {
                // clang-format off
                a_stage = std::max(acc.last_write, (acc.last_read > -1 && acc.accesses.back()->layout != paccess.layout)
                                                       ? acc.last_read
                                                       : acc.last_write) + 1;
                // clang-format on
            } else {
                ENG_WARN("Unrecognized Access type. Skipping.");
                assert(false);
                continue;
            }

            stage_idx = std::max(stage_idx, (uint32_t)std::max(0, a_stage));
            auto& res = get_resource(paccess.resource);

            if(res.is_buffer()) {
                stage.buffer_resources.push_back(paccess.resource);
                stage.buffer_barriers.push_back(Vks(VkBufferMemoryBarrier2{
                    .srcStageMask = acc.accesses.empty() ? VK_PIPELINE_STAGE_2_NONE : acc.accesses.back()->stage,
                    .srcAccessMask = acc.accesses.empty() ? VK_ACCESS_2_NONE : acc.accesses.back()->access,
                    .dstStageMask = paccess.stage,
                    .dstAccessMask = paccess.access,
                    .buffer = reinterpret_cast<VkBuffer>(*paccess.resource), // filled out during render()
                    .offset = 0ull,
                    .size = VK_WHOLE_SIZE }));
            } else if(res.is_image()) {
                const auto is_swapchain_image = !!(res.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT);
                const auto imghandle = res.get_image();
                stage.image_resources.push_back(paccess.resource);
                stage.image_barriers.push_back(Vks(VkImageMemoryBarrier2{
                    .srcStageMask = acc.accesses.empty() ? VK_PIPELINE_STAGE_2_NONE : acc.accesses.back()->stage,
                    .srcAccessMask = acc.accesses.empty() ? VK_ACCESS_2_NONE : acc.accesses.back()->access,
                    .dstStageMask = paccess.stage,
                    .dstAccessMask = paccess.access,
                    .oldLayout = (paccess.flags & AccessFlags::FROM_UNDEFINED_LAYOUT_BIT) ? VK_IMAGE_LAYOUT_UNDEFINED
                                 : acc.accesses.empty() ? (imghandle ? r.get_image(imghandle).current_layout : VK_IMAGE_LAYOUT_UNDEFINED)
                                                        : acc.accesses.back()->layout,
                    .newLayout = paccess.layout,
                    .image = reinterpret_cast<VkImage>(*paccess.resource), // filled out during render(), cast for lookup in initial barrier generation
                    .subresourceRange = { .aspectMask = imghandle ? r.get_image(imghandle).deduce_aspect() : VK_IMAGE_ASPECT_COLOR_BIT,
                                          .levelCount = VK_REMAINING_MIP_LEVELS,
                                          .layerCount = VK_REMAINING_ARRAY_LAYERS } }));
            } else {
                ENG_WARN("Unhandled resource type");
                assert(false);
                continue;
            }
            acc.accesses.push_back(&paccess);
        }

        // 1. update access history stage indices
        for(auto& a : p.accesses) {
            if(a.type & AccessType::READ_BIT) {
                accesses.at(a.resource).first_read = std::min(accesses.at(a.resource).first_read, (int32_t)stage_idx);
                accesses.at(a.resource).last_read = stage_idx;
            }
            if(a.type & AccessType::WRITE_BIT) {
                accesses.at(a.resource).first_write = std::min(accesses.at(a.resource).first_write, (int32_t)stage_idx);
                accesses.at(a.resource).last_write = stage_idx;
            }
        }

        // find actual stage, having considered all accesses to resources specified in this pass.
        Stage& dst_stage = get_stage(stage_idx);
        dst_stage.buffer_resources.insert(dst_stage.buffer_resources.end(), stage.buffer_resources.begin(),
                                          stage.buffer_resources.end());
        dst_stage.image_resources.insert(dst_stage.image_resources.end(), stage.image_resources.begin(),
                                         stage.image_resources.end());
        dst_stage.buffer_barriers.insert(dst_stage.buffer_barriers.end(), stage.buffer_barriers.begin(),
                                         stage.buffer_barriers.end());
        dst_stage.image_barriers.insert(dst_stage.image_barriers.end(), stage.image_barriers.begin(),
                                        stage.image_barriers.end());
        dst_stage.passes.push_back(&p);
    }

    // 1. update oldLayout in first barrier of each image resource to newLayout of it's last barrier, to complete the rendering cycle
    // 2. transition pre-rendergraph layouts of images to point 1.
    std::vector<VkImageMemoryBarrier2> initial_barriers;
    for(auto& a : accesses) {
        auto& res = get_resource(a.first);
        if(res.is_buffer()) { continue; }
        auto handle = res.get_image();
        const auto first_stage_index = std::min(a.second.first_read, a.second.first_write);
        assert(first_stage_index < stages.size());
        auto& first_stage = get_stage(first_stage_index);
        auto& first_barrier =
            first_stage.image_barriers.at(std::distance(first_stage.image_resources.begin(),
                                                        std::find_if(first_stage.image_resources.begin(),
                                                                     first_stage.image_resources.end(),
                                                                     [&a](const auto& b) { return b == a.first; })));
        const auto& last_barrier = a.second.accesses.back();
        if(a.second.accesses.front()->flags & AccessFlags::FROM_UNDEFINED_LAYOUT_BIT) { continue; }
        for(auto i = 0u; i < res.get_resource_count(); ++i) {
            auto& img = handle ? r.get_image(handle) : r.swapchain.images[i];
            initial_barriers.push_back(Vks(VkImageMemoryBarrier2{ .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                                                                  .srcAccessMask = VK_ACCESS_2_NONE,
                                                                  .dstStageMask = first_barrier.dstStageMask,
                                                                  .dstAccessMask = first_barrier.dstAccessMask,
                                                                  .oldLayout = first_barrier.oldLayout,
                                                                  .newLayout = last_barrier->layout,
                                                                  .image = img.image,
                                                                  .subresourceRange = first_barrier.subresourceRange }));
            img.current_layout = initial_barriers.back().newLayout;
            res.advance();
            handle = res.get_image();
        }
        first_barrier.oldLayout = last_barrier->layout;
    }
    auto initial_dep_info = Vks(VkDependencyInfo{ .imageMemoryBarrierCount = static_cast<uint32_t>(initial_barriers.size()),
                                                  .pImageMemoryBarriers = initial_barriers.data() });
    auto cmd = r.get_frame_data().cmdpool->begin();
    vkCmdPipelineBarrier2(cmd, &initial_dep_info);
    r.get_frame_data().cmdpool->end(cmd);
    r.submit_queue->with_cmd_buf(cmd).submit_wait(-1ull);
}

void RenderGraph::render(VkCommandBuffer cmd) {
    auto& r = *RendererVulkan::get_instance();
    for(auto& s : stages) {
        for(auto i = 0u; i < s.buffer_barriers.size(); ++i) {
            auto& res = get_resource(s.buffer_resources.at(i));
            s.buffer_barriers.at(i).buffer = r.get_buffer(res.get_buffer()).buffer;
            res.advance();
        }
        for(auto i = 0u; i < s.image_barriers.size(); ++i) {
            auto& res = get_resource(s.image_resources.at(i));
            auto img = res.get_image();
            res.advance();
            if(!img) {
                assert(res.flags & ResourceFlags::SWAPCHAIN_IMAGE_BIT);
                s.image_barriers.at(i).image = r.swapchain.get_current_image().image;
            } else {
                s.image_barriers.at(i).image = r.get_image(res.get_image()).image;
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
            if(pass.pipeline->pipeline) { vkCmdBindPipeline(cmd, pass.pipeline->bind_point, pass.pipeline->pipeline); }
            pass.render(cmd);
        }
    }
}

void RenderGraph::clear_passes() { passes.clear(); }

Resource& RenderGraph::get_resource(Handle<Resource> handle) { return resources.at(handle); }