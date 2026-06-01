#ifndef WIREFRAME_COMMON_HLSLI
#define WIREFRAME_COMMON_HLSLI

#include "assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_UINT GPUEngConstantsBufferIndex;
    ENG_UINT GPUVertexPositionBufferIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

#define gGPUEngConstants get_grwb(GPUEngConstants, 0)
#define gGPUVertexPosition(index) get_grwb(GPUVertexPosition, index)

struct VS_OUT
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

#endif