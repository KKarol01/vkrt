#include "assets/shaders/common.hlsli"

struct PushConstants
{
    ENG_UINT NoiseBufferIndex;
    ENG_UINT OutNoiseRWTextureIndex;
};
[[vk::push_constant]] PushConstants pc;

[numthreads(8, 8, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 outsize;
    gRWTextures2Dfloat2[pc.OutNoiseRWTextureIndex].GetDimensions(outsize.x, outsize.y);
    
    if (any(thread_id.xy >= outsize))
    {
        return;
    }
 
    uint noiseidx = (thread_id.y * outsize.x + thread_id.x) * 2;
    gRWTextures2Dfloat2[pc.OutNoiseRWTextureIndex][thread_id.xy] = gRWBuffers[pc.NoiseBufferIndex].Load<float2>((noiseidx) * sizeof(float));
}