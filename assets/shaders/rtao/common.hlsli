#ifndef RTAO_COMMON_HLSLI
#define RTAO_COMMON_HLSLI

#include "./assets/shaders/common.hlsli"

struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
    ENG_TYPE_UINT GPUEngAOSettingsBufferIndex;
    ENG_TYPE_UINT DepthTextureIndex;
    ENG_TYPE_UINT NormalTextureIndex;
    ENG_TYPE_UINT SceneTlasIndex;
    ENG_TYPE_UINT AOImageIndex;
};
[[vk::push_constant]] PushConstants pc;

#endif