#include "default_z_prepass.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUMeshPassParams)

VS_OUT main(uint vertex_index : SV_VertexID)
{
    VS_OUT output;
    
    const GPUEngConstants consts = pc.engine.load();
    float3 pos = consts.vertex_positions.load(vertex_index).pos;
    output.pos = mul(consts.proj_view, float4(pos, 1.0));
    
    return output;
}
