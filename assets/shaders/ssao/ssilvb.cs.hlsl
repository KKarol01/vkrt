#include "common.hlsli"
#define LOCAL_SIZE 8

static const uint N_SECTORS = 32u;

// Paper
// https://arxiv.org/abs/2301.11376
// Code adjusted from
// https://cybereality.com/screen-space-indirect-lighting-with-visibility-bitmask-improvement-to-gtao-ssao-real-time-ambient-occlusion-algorithm-glsl-shader-implementation/

uint count_ones(uint value)
{
    value = value - ((value >> 1u) & 0x55555555u);
    value = (value & 0x33333333u) + ((value >> 2u) & 0x33333333u);
    return ((value + (value >> 4u) & 0xF0F0F0Fu) * 0x1010101u) >> 24u;
}

uint update_sectors(float min_horizon, float max_horizon, uint bitfield)
{
    uint first_bit = uint(min_horizon * float(N_SECTORS));
    uint horizon_angle = uint(ceil((max_horizon - min_horizon) * float(N_SECTORS)));
    uint bit_span = horizon_angle > 0u ? uint(0xFFFFFFFFu >> (N_SECTORS - horizon_angle)) : 0u;
    uint cur_bitfield = bit_span << first_bit;
    return bitfield | cur_bitfield;
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint2 dims;
    gOutAOImage.GetDimensions(dims.x, dims.y);
	const GPUEngConstants consts = get_grwb(GPUEngConstants, 0);
    float2 thread_uv = (float2(dtid.xy) + 0.5f) / float2(dims.xy);
	
    if(any(dtid.xy >= dims)) { return; }
    
    float visibility = 0.0f;
    float3 lighting = 0.0f;
	float2 frontBackHorizon = 0.0;
    const float2 aspect = dims.yx / dims.x;
    
    const float depth = gDepthTexture.SampleLevel(gSamplerNearest, thread_uv, 0).x;
    const float3 position = depth_to_view_pos(dtid.xy, dims, depth);
	const float3 normal = mul(consts.view, float4(get_gt(Normal).SampleLevel(gSamplerNearest, thread_uv, 0).xyz, 0.0)).xyz;
    const float3 camera = normalize(-position);
    
    const float sample_radius = 8.0;
    const float n_slices = 4.0;
    const float n_samples = 32.0;    
    const float hitThickness = 0.15;
    
	float proj_radius = (sample_radius * consts.proj[0][0]) / -position.z;
	float step_size = proj_radius / (n_samples + 1.0);
	
    float sliceRotation = ENG_TWO_PI / n_slices;
    float jitter = IGN(int(dtid.x), int(dtid.y)) - 0.5; // -0.5, +0.5 subpixel jitter
	for (float slice = 0; slice < n_slices + 0.5; slice += 1.0) 
    {
        const float phi = sliceRotation * (slice + jitter) + ENG_PI;
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
			float sampleStep = (currentSample + jitter) * step_size + 0.01;
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
			//uint indirect = update_sectors(frontBackHorizon.x, frontBackHorizon.y, 0u);
			
			float stepVisibility = float(count_ones(indirect & ~occlusion) / float(N_SECTORS));
			lighting += stepVisibility * sampleLight * clamp(dot(normal, sampleHorizon), 0.0, 1.0) * clamp(dot(sampleNormal, -sampleHorizon), 0.0, 1.0);

			occlusion |= indirect;
		}
		
        visibility += 1.0 - float(count_ones(occlusion)) / float(N_SECTORS);
    }

    visibility /= n_slices;
    lighting /= n_slices;
    
    float4 current_signal = float4(lighting, visibility);

    // =========================================================================
    // SPATIOTEMPORAL ACCUMULATION
    // =========================================================================
    
    // // 1. fetch velocity and reproject
    // Texture2D<float2> tex_velocity = get_gt(Velocity, float2);
    // float2 velocity = tex_velocity.SampleLevel(gSamplerNearest, thread_uv, 0);
    // float2 prev_uv = thread_uv - velocity;
    
    // bool valid_history = true;
    
    // // Out-of-bounds check
    // if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f)) 
    // {
        // valid_history = false;
    // }
    
    // // 2. Disocclusion Check (Using View-Space Z for highly accurate rejection)
    // if (valid_history)
    // {
        // Texture2D<float> tex_prev_depth = get_gt(PrevDepth, float); 
        // Texture2D<float4> tex_prev_normal = get_gt(PrevNormal, float4); 

        // float prev_depth_val = tex_prev_depth.SampleLevel(gSamplerNearest, prev_uv, 0).x;
        // float3 prev_normal = mul(consts.prev_view, float4(tex_prev_normal.SampleLevel(gSamplerNearest, prev_uv, 0).xyz, 0.0)).xyz;
        
        // // Unproject the history depth to view space
        // float3 prev_pos = unproject_inf_revz_depth(float3(prev_uv * 2.0f - 1.0f, prev_depth_val));
        
        // // Compare view-space Z distance (e.g., 0.2 meters threshold)
        // if (abs(position.z - prev_pos.z) > 0.2f) 
        // {
            // valid_history = false;
        // }
        // // Compare view-space Normals (approx 25 degree threshold)
        // else if (dot(normal, prev_normal) < 0.9f) 
        // {
            // valid_history = false;
        // }
    // }
    
    // // 3. Accumulate
    // float4 output_signal = current_signal;
    // float history_length = 1.0f;
    
    // // We need an RW texture for tracking current history length, and a Texture2D for reading the old one.
    // RWTexture2D<float> out_history_len = get_grwt(HistoryLength, float);
    
    // if (valid_history)
    // {
        // Texture2D<float4> tex_history_gi = get_gt(HistoryAOImage, float4);
        // Texture2D<float> tex_prev_history_len = get_gt(PrevHistoryLength, float);
        
        // // Linear sample the history to smooth sub-pixel sub-jitter motion
        // float4 history_signal = tex_history_gi.SampleLevel(gSamplerLinear, prev_uv, 0);
        // float prev_len = tex_prev_history_len.SampleLevel(gSamplerNearest, prev_uv, 0);
        
        // // Cap history at 16 frames (or 32, tune to your liking)
        // history_length = min(prev_len + 1.0f, 16.0f);
        
        // float alpha = 1.0f / history_length;
        // output_signal = lerp(history_signal, current_signal, alpha);
    // }
    
    // // Write out final blended result and history length
    // gOutAOImage[dtid.xy] = output_signal;
    // out_history_len[dtid.xy] = history_length;
}
