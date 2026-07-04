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
    Texture2D<float4> in_ao = gTextures2Dfloat4[pc.AOTextureIndex];
	
    uint2 dims;
    in_out_color.GetDimensions(dims.x, dims.y);
    if(any(thread_id.xy >= dims.xy)) { return; } 
	
	float2 uv = (float2(thread_id.xy) + 0.5) / float2(dims);
	float4 ao = in_ao.SampleLevel(gSamplerNearest, uv, 0);
    in_out_color[thread_id.xy] = (in_out_color[thread_id.xy]+ float4(ao.rgb * 3, 0.0)) * pow(ao.w, 1.0);
}