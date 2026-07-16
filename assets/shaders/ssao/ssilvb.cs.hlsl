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
    const GPUEngConstants consts = eng_get_pc_rw_buf(GPUEngConstants, 0);
    const GPUEngConstants prev_consts = eng_get_rw_buf(GPUEngConstants, pc.PrevGPUEngConstantsBufferIndex, 0);
    float2 thread_uv = (float2(dtid.xy) + 0.5f) / float2(dims.xy);
    
    if(any(dtid.xy >= dims)) { return; }
    
    float visibility = 0.0f;
    float3 lighting = 0.0f;
    float2 frontBackHorizon = 0.0;
    
    const float depth = gDepthTexture.SampleLevel(gSamplerNearest, thread_uv, 0).x;
    const float3 position = depth_to_view_pos(dtid.xy, dims, depth);
    
    const float3 world_normal = get_gt(Normal).SampleLevel(gSamplerNearest, thread_uv, 0).xyz;
    float3 normal = normalize(mul(consts.view, float4(world_normal, 0.0)).xyz);
    
    const float3 camera = normalize(-position); 
    
    const float sample_radius = 8.0;
    const float n_slices = 16.0;
    const float n_samples = 64.0;    
    const float hitThickness = 1.0;
    
	const float2 aspect = float2(dims.yx) / dims.x;
    
    float proj_radius = (sample_radius * consts.proj[0][0]) / -position.z;
    float step_size = proj_radius / (n_samples + 1.0);
    
    // Outer loop covers a full 360 degrees
    float sliceRotation = ENG_TWO_PI / n_slices; 
    
    // High-quality noise
    float frame_seed = float(consts.frame_index % 4);
    float u1 = IGN(dtid.x, dtid.y) - 0.5;
    float u2 = IGN(dtid.x, dtid.y) - 0.5;
    
    float slice_jitter = u1 - 0.5; 
    float step_jitter  = u2 - 0.5; 
    
    for (float slice = 0; slice < n_slices + 0.5; slice += 1.0) 
    {
        const float phi = sliceRotation * (slice + slice_jitter) + ENG_PI;
        const float2 omega = float2(cos(phi), sin(phi));
        
        // Vulkan UVs: View +Y (Up) maps to UV -Y (lower V value)
        const float2 uv_omega = float2(omega.x, -omega.y);
        
        const float3 direction = float3(omega.x, omega.y, 0.0);
        
        const float3 orthoDirection = direction - dot(direction, camera) * camera;
        const float3 axis = normalize(cross(direction, camera));
        const float3 projNormal = normal - dot(normal, axis) * axis;
        const float projLength = length(projNormal);

        // Calculate normal tilt (n) exactly as CDRIN does
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
            
            // Calculate front and back horizons exactly as CDRIN does [cite: 104, 119]
            frontBackHorizon.x = dot(sampleHorizon, camera);
            frontBackHorizon.y = dot(normalize(sampleDistance - camera * hitThickness), camera);

            frontBackHorizon = acos(clamp(frontBackHorizon, -1.0, 1.0));
            frontBackHorizon = clamp((frontBackHorizon + n + ENG_HALF_PI) / ENG_PI, 0.0, 1.0);

            // Prevent unsigned integer underflow
            float minHorizon = min(frontBackHorizon.x, frontBackHorizon.y);
            float maxHorizon = max(frontBackHorizon.x, frontBackHorizon.y);
            
            uint indirect = update_sectors(minHorizon, maxHorizon, 0u);
            uint new_bits = indirect & ~occlusion;
            
            // Optimization: Skip texture fetches if light path is blocked
            if(new_bits == 0u) { continue; } 
             
            // Normal unpack (Ensure your G-Buffer values map correctly to [-1, 1] if needed)
            float3 sampleNormal = normalize(mul(consts.view, float4(get_gt(Normal).SampleLevel(gSamplerNearest, sampleUV, 0).xyz, 0.0)).xyz);
            float3 sampleLight = get_gt(Color).SampleLevel(gSamplerNearest, sampleUV, 0).xyz;
            
            // Accumulate exact fraction of un-occluded light 
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
	gOutAOImage[dtid.xy] = float4(lighting.xyz, visibility);
}