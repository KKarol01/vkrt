#ifndef NORMAL_RECONSTRUCTION_COMMON_H
#define NORMAL_RECONSTRUCTION_COMMON_H

#include "assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_UINT GPUEngConstantsBufferIndex;
    ENG_UINT DepthRWTextureIndex;
    ENG_UINT NormalRWTextureIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

#endif