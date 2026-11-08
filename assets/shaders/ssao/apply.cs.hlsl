#include "ssao.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUSSAOApplyParams)

static const uint LOCAL_SIZE = 8;

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    RWTexture2D<float4> in_out_color = pc.color.get();
    RWTexture2D<float4> in_ao = pc.ao.get();
	
    uint2 dims;
    in_ao.GetDimensions(dims.x, dims.y);
    if(any(thread_id.xy >= dims.xy)) { return; } 
	
    in_out_color[thread_id.xy] = in_out_color[thread_id.xy] * in_ao[thread_id.xy];
}
