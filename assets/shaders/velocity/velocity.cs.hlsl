#define LOCAL_SIZE 8

#include "assets/shaders/common.hlsli"

struct PushConstants
{
    ENG_UINT GPUEngConstantsBufferIndex;
    ENG_UINT PrevGPUEngConstantsBufferIndex;
    ENG_UINT DepthTextureIndex;
    ENG_UINT VelocityRWTextureIndex;
};
[[vk::push_constant]] PushConstants pc;

#include "assets/shaders/util.hlsli"

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    Texture2D<float> tex_depth = get_gt2(Depth, float);
    RWTexture2D<float2> out_vel = get_grwt2(Velocity, float2);
    const GPUEngConstants consts = get_grwb(GPUEngConstants, 0);
    const GPUEngConstants prev_consts = get_grwbi(GPUEngConstants, pc.PrevGPUEngConstantsBufferIndex, 0);
    
    uint2 dims;
    out_vel.GetDimensions(dims.x, dims.y);
    
    if(any(dtid.xy >= dims)) { return; }
    
    float2 dtuv = (float2(dtid.xy) + 0.5) / float2(dims.xy);
    float depth = tex_depth.SampleLevel(gSamplerNearest, dtuv, 0);
    
    if(depth < 1e-5) { out_vel[dtid.xy] = 0.0; return; }
    
    float4 cvpos = float4(depth_to_view_pos(dtid.xy, dims, depth), 1.0);
    float4 pvpos = mul(prev_consts.inv_view, cvpos);
    pvpos = mul(prev_consts.view, pvpos);
    pvpos = mul(prev_consts.proj, pvpos);
    pvpos.xy /= pvpos.w;
    out_vel[dtid.xy] = dtuv.xy - (pvpos.xy * 0.5 + 0.5);
}