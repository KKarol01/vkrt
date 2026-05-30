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
void main(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 dims;
    gOutAOImage.GetDimensions(dims.x, dims.y);
    if(any(thread_id.xy >= dims)) { return; }
    
	const GPUEngConstants consts = get_gsb(GPUEngConstants, 0);
	
    float2 thread_uv = (float2(thread_id.xy) + 0.5f) / float2(dims.xy);
    
    float visibility = 0.0f;
    float3 indirect_lighting = 0.0f;
    const float2 aspect = dims.yx / dims.x;
    
    const float depth = gDepthTexture.Load(int3(thread_id.xy, 0)).x;
    
    // Ensure these functions map correctly to your projection matrices
    const float3 position = depth_to_view_pos(thread_id.xy, dims, depth);
	const float3 normal = mul(consts.view, float4(get_gi(Normal).SampleLevel(gSamplerNearest, thread_uv, 0).rgb, 0.0)).xyz;
    const float3 camera = normalize(-position);
    
    const float sample_radius = 0.8f;
    const int n_slices = 4;     // Reduced for realtime performance scaling
    const int n_samples = 4;    
    const float hitThickness = 0.4; // Scale based on scene scale units
    
    float sliceRotation = ENG_PI / float(n_slices); // Slices should sweep a full semicircle (PI)
    
    float sampleScale = (sample_radius * consts.proj[0][0]) / max(-position.z, 0.001f); 
    float jitter = IGN(int(thread_id.x), int(thread_id.y)) - 0.5;

	uint occlusion_bitfield = 0u; 
	uint indirect_bitfield = 0u;

    for (int slice = 0; slice < n_slices; ++slice) 
    {
        float3 slice_lighting = 0.0f;

        float phi = sliceRotation * (float(slice) + jitter);
        float2 omega = float2(cos(phi), sin(phi));
        
        // Search direction vector
        const float3 direction = float3(omega.x, omega.y, 0.0f);
        const float3 orthoDirection = direction - dot(direction, camera) * camera;
        const float3 axis = normalize(cross(direction, camera));
        const float3 projNormal = normal - axis * dot(normal, axis);
        const float projLength = length(projNormal) + 1e-5f;

        const float signN = sign(dot(orthoDirection, projNormal));
        const float cosN = clamp(dot(projNormal, camera) / projLength, -1.0f, 1.0f);
        const float n = signN * acos(cosN);

        // Sample along both sides of the slice direction (positive and negative)
        for (int side = 0; side < 2; ++side)
        {
            float side_sign = (side == 0) ? 1.0f : -1.0f;
            
            for (int currentSample = 0; currentSample < n_samples; ++currentSample) 
            {
                float sampleStep = (float(currentSample) + jitter) / float(n_samples);
                float2 sampleUV = thread_uv + (side_sign * sampleStep * sampleScale * omega * aspect);
                
                if(any(sampleUV < 0.0f) || any(sampleUV > 1.0f)) continue;

                float sampleDepth = gDepthTexture.SampleLevel(gSamplerStates[ENG_SAMPLER_LINEAR], sampleUV, 0).x;
                float3 samplePosition = depth_to_view_pos(uint2(sampleUV * dims), dims, sampleDepth);
                
                float3 sampleDistance = samplePosition - position;
                float sampleLength = length(sampleDistance);
                
                float3 sampleHorizon = sampleDistance / (sampleLength + 1e-5f);

                float2 frontBackHorizon;
                frontBackHorizon.x = dot(sampleHorizon, camera);
                frontBackHorizon.y = dot(normalize(sampleDistance - camera * hitThickness), camera);

                frontBackHorizon = acos(clamp(frontBackHorizon, -1.0f, 1.0f));
                
                // Project horizons relative to surface tangent plane 
                frontBackHorizon.x = clamp((frontBackHorizon.x + n + ENG_HALF_PI) / ENG_PI, 0.0f, 1.0f);
                frontBackHorizon.y = clamp((frontBackHorizon.y + n + ENG_HALF_PI) / ENG_PI, 0.0f, 1.0f);

                // Update structural bitmask coverage maps
                indirect_bitfield = update_sectors(frontBackHorizon.x, frontBackHorizon.y, 0u);
                
                // Sample color buffer illumination
                float3 sampleLight = gColorTexture.SampleLevel(gSamplerStates[ENG_SAMPLER_LINEAR], sampleUV, 0).rgb;
                float3 sampleNormal = float4(get_gi(Normal).SampleLevel(gSamplerNearest, sampleUV, 0).rgb, 0.0).xyz;  
				
                // Compute indirect light components masked out by geometric blocking
                float visibility_weight = 1.0f - (float(count_ones(indirect_bitfield & ~occlusion_bitfield)) / float(N_SECTORS));
                slice_lighting += visibility_weight * sampleLight * clamp(dot(normal, sampleHorizon), 0.0f, 1.0f) * clamp(dot(sampleNormal, -sampleHorizon), 0.0f, 1.0f);
                
                occlusion_bitfield |= indirect_bitfield;
            }
        }
        
        visibility += 1.0f - (float(count_ones(occlusion_bitfield)) / float(N_SECTORS));
        indirect_lighting += slice_lighting / float(n_samples * 2);
    }

    visibility /= float(n_slices);
    indirect_lighting /= float(n_slices);
    
    // Output AO to R Channel, and optional lighting inside GBA channels if needed
    gOutAOImage[thread_id.xy] = float4(visibility.xxx, 1.0f);
    gOutAOImage[thread_id.xy] += float4(indirect_lighting.xyz, 1.0f)*0.0;
}