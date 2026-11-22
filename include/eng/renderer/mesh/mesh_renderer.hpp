#pragma once

#include <eng/renderer/renderer_fwd.hpp>
#include <eng/renderer/rendergraph.hpp>
#include <eng/ecs/components.hpp>
#include <span>

namespace eng
{
namespace gfx
{

class MeshRenderer
{
    struct MeshletInstance
    {
        Handle<Material> material;
        Handle<Pipeline> pipeline;
        u32 gpu_resource;
        u32 meshlet;
    };

    struct PassData
    {
        struct InstanceBatch
        {
            Handle<Pipeline> pipeline;
            u32 first_command;
            u32 command_count;
        };

        Handle<Buffer> instance_buf;
        Handle<Buffer> indirect_buf;
        size_t indirect_cmds_offset{};
        std::vector<InstanceBatch> batches_vec;
    };

    struct SetupPassData
    {
        RGBuffer constants;
        RGBuffer indirect;
        RGBuffer index;
        RGBuffer gpuinstances;
    };

    struct BuildPassResult
    {
        std::vector<u32> counts_vec;
        std::vector<std::byte> cmds_vec;
        std::vector<GPUInstanceId> gpuinstanceids_vec;
    };

    using InstancesVec = std::vector<MeshletInstance>;

  public:
    void build_passes();
    SetupPassData setup(MeshPassType type, RGBuilder& b) const;
    void draw(MeshPassType type, ICommandBuffer& cmd);

  private:
    static InstancesVec gather_instances(MeshPassType type, std::span<const ecs::EntityId> entities);
    static BuildPassResult build_pass(PassData& pass, const InstancesVec& vec);
    static void upload(MeshPassType type, PassData& pass, const BuildPassResult& res);

    u64 m_meshes_hash{};
    std::array<PassData, (int)MeshPassType::LAST_ENUM> m_pass_datas_arr;
};

} // namespace gfx
} // namespace eng