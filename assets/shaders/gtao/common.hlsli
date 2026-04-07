#ifndef GTAO_COMMON_H
#define GTAO_COMMON_H

#include "./assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
	ENG_TYPE_UINT GPUEngAOSettingsBufferIndex;
    ENG_TYPE_UINT DepthTextureIndex;
    ENG_TYPE_UINT NormalImageIndex;
    ENG_TYPE_UINT NoiseTextureIndex;
    ENG_TYPE_UINT AOImageIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

#endif