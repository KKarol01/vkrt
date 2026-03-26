#include "./common.hlsli"

VS_OUT main(uint vertex_index : SV_VertexID)
{
    VS_OUT output;
    
    float3 pos = gsb_get(GPUVertexPosition, vertex_index).pos;
    output.pos = mul(gsb_get(GPUEngConstants, 0).proj_view, float4(pos, 1.0));
    
    return output;
}