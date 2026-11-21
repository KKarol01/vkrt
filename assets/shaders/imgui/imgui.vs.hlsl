#include "imgui.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUImGuiParams)

VS_OUT main(uint vertex_index : SV_VertexID)
{
    VS_OUT output;
    const GPUImGuiVertex vx = pc.vertices.load(vertex_index);
    output.color = unpack_unorm4x8(vx.color);
    output.color.xyz = pow(output.color.xyz, float3(2.2, 2.2, 2.2));
    output.uv = vx.uv;
    output.pos = float4(vx.pos * pc.scale + pc.translate, 0.0, 1.0); 
    return output;
}
