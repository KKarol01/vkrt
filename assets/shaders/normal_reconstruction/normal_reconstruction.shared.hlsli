#ifndef NORMAL_RECONSTRUCTION_SHARED_HLSLI
#define NORMAL_RECONSTRUCTION_SHARED_HLSLI

#include "assets/shaders/common.hlsli"

ENG_NAMESPACE_BEGIN

struct GPUNormalReconstructionParams
{
    BufferRef<GPUEngConstants> engine;
    Texture2DRef<ENG_FLOAT> depth;
    RWTexture2DRef<ENG_FLOAT4> out_normal;
};

ENG_NAMESPACE_END

#endif
