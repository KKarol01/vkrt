#include "common.hlsli"
#define LOCAL_SIZE 8

static const uint N_SECTORS = 32u;

// Paper
// https://arxiv.org/abs/2301.11376
// Code adjusted from
// https://cybereality.com/screen-space-indirect-lighting-with-visibility-bitmask-improvement-to-gtao-ssao-real-time-ambient-occlusion-algorithm-glsl-shader-implementation/

uint count_ones(uint value)
{
    return countbits(value);
}

uint update_sectors(float min_horizon, float max_horizon, uint bitfield)
{
    uint first_bit = uint(min_horizon * float(N_SECTORS));
    uint horizon_angle = uint(ceil((max_horizon - min_horizon) * float(N_SECTORS)));
    uint bit_span = horizon_angle > 0u ? uint(0xFFFFFFFFu >> (N_SECTORS - horizon_angle)) : 0u;
    uint cur_bitfield = bit_span << first_bit;
    return bitfield | cur_bitfield;
}

float Halton(uint index, uint base)
{
    float f = 1.0f;
    float result = 0.0f;
    uint current = index;
    
    while (current > 0u)
    {
        f = f / float(base);
        result += f * float(current % base);
        current = current / base;
    }
    
    return result;
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint2 dims;
    gOutAOImage.GetDimensions(dims.x, dims.y);
    const GPUEngConstants consts = get_grwb(GPUEngConstants, 0);
    const GPUEngConstants prev_consts = get_grwbi(GPUEngConstants, pc.PrevGPUEngConstantsBufferIndex, 0);
    float2 thread_uv = (float2(dtid.xy) + 0.5f) / float2(dims.xy);
    
    if(any(dtid.xy >= dims)) { return; }
    
    float visibility = 0.0f;
    float3 lighting = 0.0f;
    float2 frontBackHorizon = 0.0;
    const float2 aspect = dims.yx / dims.x;
    
    const float depth = gDepthTexture.SampleLevel(gSamplerNearest, thread_uv, 0).x;
    const float3 position = depth_to_view_pos(dtid.xy, dims, depth);
    
    // 1. Fetch World Space Normal for history rejection
    const float3 world_normal = get_gt(Normal).SampleLevel(gSamplerNearest, thread_uv, 0).xyz;
    // 2. Transform and normalize View Space Normal for SSIL raymarching
    const float3 normal = normalize(mul(consts.view, float4(world_normal, 0.0)).xyz);
    
    const float3 camera = normalize(-position);
    
    const float sample_radius = 8.0;
    const float n_slices = 4.0;
    const float n_samples = 32.0;    
    const float hitThickness = 0.15;
    
    float proj_radius = (sample_radius * consts.proj[0][0]) / -position.z;
    float step_size = proj_radius / (n_samples + 1.0);
    
    float sliceRotation = ENG_TWO_PI / n_slices;
    
    // =========================================================================
    // SPATIOTEMPORAL JITTER (HALTON + IGN)
    // =========================================================================
    
    // Halton sequence is 1-based (passing 0 returns 0, which we want to avoid)
    uint halton_index = (consts.frame_index % 16) + 1;
    
    // Base 2 for rotation, Base 3 for step offset
    float2 halton = float2(Halton(halton_index, 2), Halton(halton_index, 3));
    
    // Base spatial noise
    float spatial_jitter = IGN(int(dtid.x), int(dtid.y));
    
    // Combine and wrap in [-0.5, 0.5]
    float slice_jitter = frac(spatial_jitter + halton.x) - 0.5f;
    float step_jitter  = frac(spatial_jitter + halton.y) - 0.5f;
    
    for (float slice = 0; slice < n_slices + 0.5; slice += 1.0) 
    {
        // Apply 1st dimension of jitter to rotation
        const float phi = sliceRotation * (slice + slice_jitter) + ENG_PI;
        const float2 omega = float2(cos(phi), sin(phi));
        const float3 direction = float3(omega.x, omega.y, 0.0);
        
        const float3 orthoDirection = direction - dot(direction, camera) * camera;
        const float3 axis = cross(direction, camera);
        const float3 projNormal = normal - dot(normal, axis) * axis;
        const float projLength = length(projNormal);

        const float signN = sign(dot(orthoDirection, projNormal));
        const float cosN = clamp(dot(projNormal, camera) / projLength, -1.0, 1.0);
        const float n = signN * acos(cosN);

        uint occlusion = 0u;

        for(float currentSample = 0.0; currentSample < n_samples + 0.5; currentSample += 1.0)
        {
            // Apply 2nd dimension of jitter to step offset
            float sampleStep = (currentSample + step_jitter) * step_size + 0.01;
            float2 sampleUV = thread_uv + (omega * sampleStep * aspect);
            
            if(any(sampleUV < 0.0) || any(sampleUV > 1.0)) { continue; }
            
            float sampleDepth = gDepthTexture.SampleLevel(gSamplerNearest, sampleUV, 0).x;
            float3 samplePosition = depth_to_view_pos(uint2(sampleUV * dims), dims, sampleDepth);
            float3 sampleNormal = mul(consts.view, float4(get_gt(Normal).SampleLevel(gSamplerNearest, sampleUV, 0).xyz, 0.0)).xyz;
            float3 sampleLight = get_gt(Color).SampleLevel(gSamplerLinear, sampleUV, 0).xyz;
            
            float3 sampleDistance = samplePosition - position;
            float sampleLength = length(sampleDistance);
            
            if(sampleLength < 1e-5) { continue; }
            
            float3 sampleHorizon = sampleDistance / sampleLength;
            
            frontBackHorizon.x = dot(sampleHorizon, camera);
            frontBackHorizon.y = dot(normalize(sampleDistance - camera * hitThickness), camera);

            frontBackHorizon = acos(clamp(frontBackHorizon, -1.0, 1.0));
            frontBackHorizon = clamp((frontBackHorizon + n + ENG_HALF_PI) / ENG_PI, 0.0, 1.0);

            float minHorizon = min(frontBackHorizon.x, frontBackHorizon.y);
            float maxHorizon = max(frontBackHorizon.x, frontBackHorizon.y);
            uint indirect = update_sectors(minHorizon, maxHorizon, 0u);
            
            float stepVisibility = float(count_ones(indirect & ~occlusion)) / float(N_SECTORS);
            //lighting += stepVisibility * sampleLight * clamp(dot(normal, sampleHorizon), 0.0, 1.0) * clamp(dot(sampleNormal, -sampleHorizon), 0.0, 1.0);

            occlusion |= indirect;
        }
        
        visibility += 1.0 - float(count_ones(occlusion)) / float(N_SECTORS);
    }

    visibility /= n_slices;
    //lighting /= n_slices;
	    gOutAOImage[dtid.xy].xyz = visibility;
	    gOutAOImage[dtid.xy].w = 1.0;
		return;
    //float4 current_signal = float4(lighting, visibility);

    //RWTexture2D<float4> history_tex = get_grwt2(OutHistory, float4);
    //RWTexture2D<float4> prev_history_tex = get_grwt2(PrevHistory, float4);
    //
    //float4 history = prev_history_tex[dtid.xy].xyzw * (12.0/16.0) + current_signal * (4.0 / 16.0);
    //history_tex[dtid.xy] = history;
    //
    //gOutAOImage[dtid.xy].xyz = (gOutAOImage[dtid.xy].xyz + history.xyz*10.0) * history.w;
}
#if 0
    // =========================================================================
    // SPATIOTEMPORAL ACCUMULATION
    // =========================================================================
    
    // 1. fetch velocity and reproject
    Texture2D<float2> tex_velocity = get_gt2(Velocity, float2);
    float2 velocity = tex_velocity.SampleLevel(gSamplerNearest, thread_uv, 0);
    float2 prev_uv = thread_uv - velocity;
    
    bool valid_history = true;
    
    // Out-of-bounds check
    if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f)) 
    {
        valid_history = false;
    }
    
    // 2. Disocclusion Check (Using WORLD-SPACE for camera-agnostic rejection)
    if (valid_history)
    {
        Texture2D<float> tex_prev_depth = get_gt2(PrevDepth, float); 
        Texture2D<float4> tex_prev_normal = get_gt2(PrevNormal, float4); 

        float prev_depth_val = tex_prev_depth.SampleLevel(gSamplerNearest, prev_uv, 0).x;
        float3 prev_world_normal = tex_prev_normal.SampleLevel(gSamplerNearest, prev_uv, 0).xyz;
        
        float3 prev_view_pos = unproject_inf_revz_depth(prev_uv, tex_prev_depth);
        
        // Transform current and previous View Space positions to World Space
        float3 prev_world_pos = mul(prev_consts.inv_view, float4(prev_view_pos, 1.0)).xyz;
        float3 current_world_pos = mul(consts.inv_view, float4(position, 1.0)).xyz;
        
        // Compare WORLD-SPACE distance (e.g., 0.2 meters threshold)
        if (distance(current_world_pos, prev_world_pos) > 0.2f) 
        {
            valid_history = false;
        }
        // Compare WORLD-SPACE Normals (approx 25 degree threshold)
        else if (dot(world_normal, prev_world_normal) < 1.1f) 
        {
            valid_history = false;
        }
    }
    
    // 3. Accumulate
    float4 output_signal = current_signal;
    float history_length = 1.0f;
    
    RWTexture2D<float> out_history_len = get_grwt2(OutHistoryLen, float);
    
    if (valid_history)
    {
        Texture2D<float4> tex_history_gi = get_gt2(PrevHistory, float4); 
        Texture2D<float> tex_prev_history_len = get_gt2(PrevHistoryLen, float);
        
        float4 history_signal = tex_history_gi.SampleLevel(gSamplerLinear, prev_uv, 0);
        float prev_len = tex_prev_history_len.SampleLevel(gSamplerNearest, prev_uv, 0);
        
        history_length = min(prev_len + 1.0f, 16.0f);
        
        float alpha = 1.0f / history_length;
        output_signal = lerp(history_signal, current_signal, alpha);
    }
    
    // Write out final blended result and history length
    get_grwt2(OutHistory, float4)[dtid.xy] = output_signal;
    out_history_len[dtid.xy] = history_length;
    
    gOutAOImage[dtid.xy].xyz = output_signal.xyz*1.0;
    gOutAOImage[dtid.xy].xyz *= output_signal.w;
}
#endif