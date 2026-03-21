#include "./common.hlsli"

VS_OUT main(uint vertex_index : SV_VertexID)
{
    VS_OUT output;

    float3 pos = gsb_get(GPUVertexPosition, vertex_index).pos;
    float4x4 projView = gsb_get(GPUEngConstants, 0).proj_view;

    output.pos = mul(projView, float4(pos, 1.0));
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    
    return output;
}