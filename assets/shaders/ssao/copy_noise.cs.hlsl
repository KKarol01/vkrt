#include "ssao.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUSSAOCopyNoiseParams)

[numthreads(8, 8, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    RWTexture2D<float2> out_noise = pc.out_noise.get();
    uint2 outsize;
    out_noise.GetDimensions(outsize.x, outsize.y);
    
    if (any(thread_id.xy >= outsize)) { return; }
 
    out_noise[thread_id.xy] = pc.noise.load(thread_id.y * outsize.x + thread_id.x);
}
