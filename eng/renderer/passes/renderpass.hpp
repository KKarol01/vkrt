#pragma once

#include <vector>
#include <cstdint>
#include <type_traits>
#include <eng/renderer/renderer_fwd.hpp>
#include <eng/common/callback.hpp>

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
    void draw(const Callback<void(const IndirectDrawParams&)>& draw_callback) const;
    std::vector<InstanceBatch> batches;
    Handle<Buffer> indirect_buf; // [counts..., commands...]
    BufferView counts_view;
    BufferView cmds_view;
};

class MeshRenderData
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