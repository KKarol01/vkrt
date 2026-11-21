#include "default_wireframe.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUMeshPassParams)

VS_OUT main(uint vertex_index : SV_VertexID, uint instance_index : SV_InstanceID)
{
    VS_OUT output;

    const GPUEngConstants consts = pc.engine.load();
    const GPUInstanceId instanceid = pc.instances.load(instance_index);
    const float4x4 transform = consts.transforms.load(instanceid.insti);
    float3 pos = consts.vertex_positions.load(vertex_index).pos;

    output.pos = mul(consts.proj_view, mul(transform, float4(pos, 1.0)));
    output.color = float4(1.0, 1.0, 1.0, 1.0);

    return output;
}
