#include "common.hlsli"

VS_OUT main(uint vertex_index : SV_VertexID)
{
    VS_OUT output;
    
    GPUEngConstants constants = get_grwb2(GPUEngConstants, pc, 0);
    float3 pos = get_grwb2(GPUVertexPosition, constants, vertex_index).pos;
    output.pos = mul(constants.proj_view, float4(pos, 1.0));
    
    return output;
}