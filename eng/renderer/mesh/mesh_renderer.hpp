#pragma once

#include <eng/renderer/renderer_fwd.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/ecs/components.hpp>

namespace eng
{
namespace gfx
{

class MeshRenderer
{
    struct PassData
    {
        struct InstacedMeshHandle
        {
            Handle<gfx::Mesh> mesh;
            uint32_t gpu_resource;
        };

        struct MeshInstance
        {
            Handle<Geometry> geometry;
            Handle<Material> material;
            Handle<Pipeline> pipeline;
            uint32_t gpu_resource;
            uint32_t meshlet;
        };

        struct InstanceBatch
        {
            Handle<Pipeline> pipeline;
            uint32_t first_command;
            uint32_t command_count;
        };

        uint64_t meshes_hash{};
        std::vector<InstacedMeshHandle> meshes_vec;

        Handle<Buffer> indirect_buf;
        size_t indirect_cmds_offset{};
        std::vector<InstanceBatch> batches_vec;
    };

    struct SetupPassData
    {
        RGAccessId indirect;
        RGAccessId index;
    };

    struct BuildPassResult
    {
        std::vector<std::byte> cmds_vec;
        uint32_t cmd_count;
    };

    using InstancesVec = std::vector<PassData::MeshInstance>;
    using PassInstancesGroups = std::vector<Range32u>;

  public:
    void instance_entity(ecs::EntityId entity);
    void build_passes();
    SetupPassData setup(MeshPassType type, RGBuilder& b) const;
    void draw(MeshPassType type, ICommandBuffer& cmd);

  private:
    static InstancesVec extract_mesh_instances(MeshPassType type, PassData& pass);
    static void sort_mesh_instances(InstancesVec& vec);
    static BuildPassResult build_pass_from_instances(PassData& pass, const InstancesVec& vec);

    std::array<PassData, (int)MeshPassType::LAST_ENUM> m_pass_datas_arr;
};

} // namespace gfx
} // namespace eng