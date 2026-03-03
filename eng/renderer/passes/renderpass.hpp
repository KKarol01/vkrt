#pragma once

#include <vector>
#include <cstdint>
#include <type_traits>
#include <eng/renderer/renderer_fwd.hpp>

namespace eng
{
namespace gfx
{

struct MeshInstance
{
    Handle<Geometry> geometry;
    Handle<Material> material;
    uint32_t instance_index;
    uint32_t meshlet_index;
};

struct InstanceBatch
{
    Handle<Pipeline> pipeline;
    uint32_t instance_count;
    uint32_t first_command;
    uint32_t command_count;
};

struct IndirectDrawParams
{
    const InstanceBatch* draw{};
    uint32_t max_draw_count{};
    size_t stride{};
    size_t command_offset_bytes{};
    size_t count_offset_bytes{};
};

struct IndirectBatch
{
    void draw(const auto& draw_callback) const
    {
        size_t cmdoffacc = 0;
        for(auto i = 0u; i < batches.size(); ++i)
        {
            const auto& batch = batches[i];
            const auto cntoff = sizeof(uint32_t) * i;
            const auto cmdsize = get_renderer().backend->get_indirect_indexed_command_size();
            const auto cmdoff = cmdsize * cmdoffacc + cmds_view.range.offset;
            draw_callback(IndirectDrawParams{
                .draw = &batch,
                .max_draw_count = batch.command_count,
                .stride = cmdsize,
                .command_offset_bytes = cmdoff,
                .count_offset_bytes = cntoff,
            });
            cmdoffacc += batch.command_count;
        }
    }
    std::vector<InstanceBatch> batches;
    Handle<Buffer> indirect_buf; // [counts..., commands...]
    BufferView counts_view;
    BufferView cmds_view;
};

class RenderPass
{
  public:
    void build();
    void add_mesh(uint32_t instance_index, Handle<gfx::Mesh> mesh);

    RenderPassType type{ RenderPassType::LAST_ENUM };
    IndirectBatch draw;
    Handle<Buffer> instance_buffer;
    BufferView instance_view;
    std::vector<MeshInstance> mesh_instances;
    std::vector<MeshInstance> meshes;
};

} // namespace gfx
} // namespace eng