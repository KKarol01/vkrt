#ifndef RTAO_COMMON_HLSLI
#define RTAO_COMMON_HLSLI

#include "assets/shaders/common.hlsli"

struct PushConstants
{
    ENG_UINT GPUEngConstantsBufferIndex;
    ENG_UINT GPUEngAOSettingsBufferIndex;
    ENG_UINT DepthTextureIndex;
    ENG_UINT NormalTextureIndex;
    ENG_UINT SceneTlasIndex;
    ENG_UINT AORWTextureIndex;
};
[[vk::push_constant]] PushConstants pc;

#endif