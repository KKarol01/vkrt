#include "common.hlsli"

#define LOCAL_SIZE 8

static const uint N_SECTORS = 32u;
static const float MAX_HISTORY_LEN = 16.0f;

// Paper: https://arxiv.org/abs/2301.11376
// Visibility Bitmask reference: https://cybereality.com/screen-space-indirect-lighting-with-visibility-bitmask-improvement-to-gtao-ssao-real-time-ambient-occlusion-algorithm-glsl-shader-implementation/

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
    const GPUEngConstants consts = eng_get_pc_rw_buf(GPUEngConstants, 0);
    const GPUEngConstants prev_consts = eng_get_rw_buf(GPUEngConstants, pc.PrevGPUEngConstantsBufferIndex, 0);
    float2 thread_uv = (float2(dtid.xy) + 0.5f) / float2(dims.xy);
    
    if(any(dtid.xy >= dims)) { return; }
    
    float visibility = 0.0f;
    float3 lighting = 0.0f;
    float2 frontBackHorizon = 0.0;
    
    const float depth = gDepthTexture.SampleLevel(gSamplerNearest, thread_uv, 0).x;
    
    // Discard background/skybox pixels to prevent history smearing
    if(depth < 1e-5) 
    {
        gOutHistoryLen[dtid.xy] = 1.0f;
        gOutHistoryTexture[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        gOutAOImage[dtid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return; 
    }
    
    const float3 position = depth_to_view_pos(dtid.xy, dims, depth);
    const float3 world_normal = gNormalTexture.SampleLevel(gSamplerNearest, thread_uv, 0).xyz;
    float3 normal = normalize(mul(consts.view, float4(world_normal, 0.0)).xyz);
    const float3 camera = normalize(-position); 
    
    const float sample_radius = 1.0;
    const float n_slices = 4.0; 
    const float n_samples = 8.0;    
    const float hitThickness = 1.0;
	const float2 aspect = float2(dims.yx) / dims.x;
    
    float proj_radius = (sample_radius * consts.proj[0][0]) / -position.z;
    float step_size = proj_radius / (n_samples + 1.0);
    float sliceRotation = ENG_TWO_PI / n_slices; 
    
    // Spatiotemporal Jitter using Halton sampling
    uint frame_idx = consts.frame_index * 1;
    float jitter_x = Halton((frame_idx % 16) + 1, 2);
    float jitter_y = Halton((frame_idx % 16) + 1, 3);
    
    // Base spatial interleaved gradient noise
    float u1 = IGN(dtid.x, dtid.y) - 0.5;
    float u2 = fmod(IGN(dtid.x, dtid.y) + 0.5, 1.0) - 0.5;
    
    // Combined Spatiotemporal jitter offsets
    float slice_jitter = u1 + jitter_x; 
    float step_jitter  = u2 + jitter_y; 
    
    // Spatial pass calculation
    for (float slice = 0; slice < n_slices + 0.5; slice += 1.0) 
    {
        const float phi = sliceRotation * (slice + slice_jitter) + ENG_PI;
        const float2 omega = float2(cos(phi), sin(phi));
        const float2 uv_omega = float2(omega.x, -omega.y);
        const float3 direction = float3(omega.x, omega.y, 0.0);
        
        const float3 orthoDirection = direction - dot(direction, camera) * camera;
        const float3 axis = normalize(cross(direction, camera));
        const float3 projNormal = normal - dot(normal, axis) * axis;
        const float projLength = length(projNormal);

        const float signN = sign(dot(orthoDirection, projNormal));
        const float cosN = clamp(dot(projNormal, camera) / projLength, -1.0, 1.0);
        const float n = signN * acos(cosN);

        uint occlusion = 0u;

        for(float currentSample = 0.0; currentSample < n_samples + 0.5; currentSample += 1.0)
        {
            float sampleStep = (currentSample + step_jitter + 0.5) * step_size + 0.01;
            float2 sampleUV = thread_uv - (uv_omega * sampleStep * aspect);
            
            if(any(sampleUV < 0.0) || any(sampleUV > 1.0)) { continue; }
            
            float sampleDepth = gDepthTexture.SampleLevel(gSamplerNearest, sampleUV, 0).x;
            float3 samplePosition = depth_to_view_pos(uint2(sampleUV * dims), dims, sampleDepth);
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
            uint new_bits = indirect & ~occlusion;
            
            if(new_bits == 0u) { continue; } 
             
            float3 sampleNormal = normalize(mul(consts.view, float4(gNormalTexture.SampleLevel(gSamplerNearest, sampleUV, 0).xyz, 0.0)).xyz);
            float3 sampleLight = gColorTexture.SampleLevel(gSamplerNearest, sampleUV, 0).xyz;
            
            float stepVisibility = float(count_ones(new_bits)) / float(N_SECTORS);
            
            lighting += stepVisibility 
                      * sampleLight 
                      * clamp(dot(normal, sampleHorizon), 0.0, 1.0) 
                      * clamp(dot(sampleNormal, -sampleHorizon), 0.0, 1.0); 

            occlusion |= indirect;
        }
        
        visibility += 1.0 - float(count_ones(occlusion)) / float(N_SECTORS);
    }
	
    visibility /= n_slices;
    lighting /= n_slices;
    
    // --- SPATIOTEMPORAL REPROJECTION ---
    
    float2 velocity = gVelocityTexture.SampleLevel(gSamplerNearest, thread_uv, 0);
    float2 prev_uv = thread_uv - velocity;
    int2 prev_coords = int2(prev_uv * dims);
    
    bool valid_history = true;
	//valid_history = false;
    if(any(prev_uv < 0.0) || any(prev_uv > 1.0)) 
    {
        valid_history = false;
    }
    
    // Neighborhood / Geometry Validation
    if(valid_history) 
    {
        float prev_depth = gPrevDepthTexture.SampleLevel(gSamplerNearest, prev_uv, 0).x;
        float3 prev_world_normal = gPrevNormalTexture.SampleLevel(gSamplerNearest, prev_uv, 0).xyz;
        
        // Unproject previous depth to world space for distance validation
        float3 prev_view_pos = depth_to_view_pos(prev_coords, dims, prev_depth);
        float3 prev_world_pos = mul(prev_consts.inv_view, float4(prev_view_pos, 1.0)).xyz;
        float3 curr_world_pos = mul(consts.inv_view, float4(position, 1.0)).xyz;
        
        // Discard history if geometry drifted too far or normals drastically deviate
        if(distance(curr_world_pos, prev_world_pos) > 0.15) 
        {
            valid_history = false;
        }
        else if(dot(world_normal, prev_world_normal) < 0.8) 
        {
            valid_history = false;
        }
    }
    
    float history_len = 0.0;
    float4 prev_history = float4(0.0, 0.0, 0.0, 1.0);
    
    if(valid_history) 
    {
        history_len = gPrevHistoryLen[prev_coords];
        prev_history = gPrevHistoryTexture[prev_coords];
    }
    
    history_len = min(history_len + 1.0, MAX_HISTORY_LEN);
    float alpha = 1.0 / history_len;
    
    float4 current_val = float4(lighting, visibility);
    float4 final_val = lerp(prev_history, current_val, alpha);
    
    // Write out states and visual buffer
    gOutHistoryLen[dtid.xy] = history_len;
    gOutHistoryTexture[dtid.xy] = final_val;
	gOutAOImage[dtid.xy] = final_val;
	//gOutAOImage[dtid.xy].rgb *= 20.0;
}