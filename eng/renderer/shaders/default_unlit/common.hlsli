#ifndef UNLIT_COMMON_H
#define UNLIT_COMMON_H

#include "./assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
    ENG_TYPE_UINT GPUVertexPositionBufferIndex;
	// uint32_t imidb;
	// uint32_t GPUFWDPLightGridsBufferIndex;
	// uint32_t GPUFWDPLightListsBufferIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

struct VS_OUT
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

#endif