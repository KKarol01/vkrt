#ifndef IMGUI_SHARED_HLSLI
#define IMGUI_SHARED_HLSLI

#include "assets/shaders/common.hlsli"

ENG_NAMESPACE_BEGIN

struct GPUImGuiVertex
{
    ENG_FLOAT2 pos;
    ENG_FLOAT2 uv;
    ENG_UINT color;
};

struct GPUImGuiParams
{
    ENG_FLOAT2 scale;
    ENG_FLOAT2 translate;
    BufferRef<GPUImGuiVertex> vertices;
    Texture2DRef<ENG_FLOAT4> texture;
};

ENG_NAMESPACE_END

#ifndef __cplusplus
struct VS_OUT
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
#endif

#endif
