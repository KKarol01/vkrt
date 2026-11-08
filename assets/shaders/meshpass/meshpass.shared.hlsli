#ifndef MESHPASS_SHARED_HLSLI
#define MESHPASS_SHARED_HLSLI

#include "assets/shaders/common.hlsli"

#ifndef __cplusplus
struct VSOutput
{
	float4 position : SV_Position;
	float4 normal : NORMAL0;
	float2 uv : TEXCOORD0;
	nointerpolation uint material_index : TEXCOORD5;
};
#endif

#endif
