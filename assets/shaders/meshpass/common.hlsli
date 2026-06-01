#ifndef MESHPASS_COMMON_H
#define MESHPASS_COMMON_H

#include "assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_UINT GPUEngConstantsBufferIndex;
    ENG_UINT GPUInstanceIdBufferIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

struct VSOutput
{
	float4 position : SV_Position;
	float4 normal : NORMAL0;
	float2 uv : TEXCOORD0;
	nointerpolation uint material_index : TEXCOORD5;
};

#endif