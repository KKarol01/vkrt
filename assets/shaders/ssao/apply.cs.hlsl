#include "assets/shaders/common.hlsli"

static const uint LOCAL_SIZE = 8;

struct PushConstants
{
    ENG_UINT ColorTextureIndex;
    ENG_UINT AOTextureIndex;
    
};
[[vk::push_constant]] PushConstants pc;

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    RWTexture2D<float4> in_out_color = gRWTextures2Dfloat4[pc.ColorTextureIndex];
    RWTexture2D<float4> in_ao = gRWTextures2Dfloat4[pc.AOTextureIndex];
	
    uint2 dims;
    in_ao.GetDimensions(dims.x, dims.y);
    if(any(thread_id.xy >= dims.xy)) { return; } 
	
    in_out_color[thread_id.xy] = in_out_color[thread_id.xy] * pow(in_ao[thread_id.xy], 1.0);
}