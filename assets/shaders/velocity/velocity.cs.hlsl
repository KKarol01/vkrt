#include "velocity.shared.hlsli"
#include "assets/shaders/util.hlsli"

ENG_PUSH_CONSTANTS(GPUVelocityParams)

#define LOCAL_SIZE 8

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    Texture2D<float> tex_depth = pc.depth.get();
    RWTexture2D<float2> out_vel = pc.out_velocity.get();
    const GPUEngConstants consts = pc.engine.load();
    const GPUEngConstants prev_consts = pc.prev_engine.load();
    
    uint2 dims;
    out_vel.GetDimensions(dims.x, dims.y);
    
    if(any(dtid.xy >= dims)) { return; }
    
    float2 dtuv = (float2(dtid.xy) + 0.5) / float2(dims.xy);
    float depth = tex_depth.SampleLevel(gSamplerNearest, dtuv, 0);
    
    if(depth < 1e-5) { out_vel[dtid.xy] = 0.0; return; }
    
    float4 cvpos = float4(depth_to_view_pos(consts.proj, dtid.xy, dims, depth), 1.0);
    float4 pvpos = mul(consts.inv_view, cvpos);
    pvpos = mul(prev_consts.view, pvpos);
    pvpos = mul(consts.proj, pvpos);
    pvpos.xy /= pvpos.w;
    out_vel[dtid.xy] = dtuv.xy - (pvpos.xy * 0.5 + 0.5);
}
