#ifndef WIREFRAME_COMMON_HLSLI
#define WIREFRAME_COMMON_HLSLI

#include "assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
    ENG_TYPE_UINT GPUVertexPositionBufferIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

#define gGPUEngConstants get_gsb(GPUEngConstants, 0)
#define gGPUVertexPosition(index) get_gsb(GPUVertexPosition, index)

struct VS_OUT
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

#endif