#include "./common.hlsli"

float4 unpackUnorm4x8(uint col)
{
    float4 res;
    res.x = (float) ((col >> 0) & 0xFF) / 255.0f; // Red
    res.y = (float) ((col >> 8) & 0xFF) / 255.0f; // Green
    res.z = (float) ((col >> 16) & 0xFF) / 255.0f; // Blue
    res.w = (float) ((col >> 24) & 0xFF) / 255.0f; // Alpha
    return res;
}

VS_OUT main(uint vertex_index : SV_VertexID)
{
    VS_OUT output;
    ImGuiVertex vx = gsb_get(ImGuiVertex, vertex_index);
    output.color = unpackUnorm4x8(vx.color);
    output.color.xyz = pow(output.color.xyz, float3(2.2, 2.2, 2.2));
    output.uv = vx.uv;
    output.pos = float4(vx.pos * pc.scale + pc.translate, 0.0, 1.0);
    return output;
}