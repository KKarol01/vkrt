#ifndef IMGUI_COMMON_H
#define IMGUI_COMMON_H

#include "./assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    float2 scale;
    float2 translate;
    uint ImGuiVertexBufferIndex;
    uint color_tex;
};
[[vk::push_constant]] PushConstants pc;
#endif

struct VS_OUT
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct ImGuiVertex
{
    float2 pos;
    float2 uv;
    uint color;
};

#endif