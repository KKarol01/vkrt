#ifndef GTAO_COMMON_H
#define GTAO_COMMON_H

#include "assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_UINT GPUEngConstantsBufferIndex;
    ENG_UINT GPUEngAOSettingsBufferIndex;
    ENG_UINT DepthTextureIndex;
    ENG_UINT NormalRWTextureIndex;
    ENG_UINT NoiseTextureIndex;
    ENG_UINT AORWTextureIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

#include "assets/shaders/util.hlsli"

#endif