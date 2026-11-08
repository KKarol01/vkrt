#ifndef VELOCITY_SHARED_HLSLI
#define VELOCITY_SHARED_HLSLI

#include "assets/shaders/common.hlsli"

ENG_NAMESPACE_BEGIN

struct GPUVelocityParams
{
    BufferRef<GPUEngConstants> engine;
    BufferRef<GPUEngConstants> prev_engine;
    Texture2DRef<ENG_FLOAT> depth;
    RWTexture2DRef<ENG_FLOAT2> out_velocity;
};

ENG_NAMESPACE_END

#endif
