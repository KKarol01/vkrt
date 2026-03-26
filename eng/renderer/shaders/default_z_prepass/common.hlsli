#ifndef Z_PREPASS_COMMON_H
#define Z_PREPASS_COMMON_H

#include "./assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
    ENG_TYPE_UINT GPUVertexPositionBufferIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

struct VS_OUT
{
    float4 pos : SV_Position;
};

#endif