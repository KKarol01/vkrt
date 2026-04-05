#ifndef NORMAL_RECONSTRUCTION_COMMON_H
#define NORMAL_RECONSTRUCTION_COMMON_H

#include "./assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
    ENG_TYPE_UINT DepthImageIndex;
    ENG_TYPE_UINT NormalImageIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

#endif