#include "common.hlsli"

static const uint LOCAL_SIZE = 8;
static const uint SAMPLE_COUNT = 64;
static const uint N_SECTORS = 32u;

float3 hash3df(float2 value)
{
	float3 p3 = frac(float3(value.xyx) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yxz+33.33);
	return frac((p3.xxy+p3.yzz)*p3.zyx);
}


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
	uint3 dtid = thread_id;
#if 1
return;
    uint2 dims;
    gOutAOImage.GetDimensions(dims.x, dims.y);
    if (any(thread_id.xy >= dims.xy)) { return; }

    const float2 uv = (float2(thread_id.xy) + 0.5) / float2(dims);
    float depth = gDepthTexture.Load(int3(thread_id.xy, 0)); 

    if (depth < 1e-5) 
    { 
        gOutAOImage[thread_id.xy] = float4(1.0, 1.0, 1.0, 1.0); 
        return; 
    }

    //GPUEngAOSettings in_ao_settings = get_grwb(GPUEngAOSettings, 0);
    const float radius = 0.8; //in_ao_settings.Radius;
    const float bias = 0.01; //in_ao_settings.Bias;

    float3 vs_pos = depth_to_view_pos(thread_id.xy, dims, depth);
    float3 normal = calculate_normal_from_depth(int2(thread_id.xy), int2(dims), gDepthTexture);

    float2 noise_scale = float2(dims) / 4.0; 
    float3 random_vec = float3(gNoiseTexture.SampleLevel(gSamplerStates[ENG_SAMPLER_LINEAR], uv * noise_scale, 0).rg, 0.0);

    float3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    const float4x4 proj = get_grwb(GPUEngConstants, 0).proj;
    float occlusion = 0.0;
    
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        // Generate uniform hemisphere sample oriented along +Z normal axis via hash
        float3 rand_sample = hash3df(uv + float(i) * float2(0.1173, 0.7341));
        float phi = rand_sample.x * 2.0 * ENG_PI;
        float cos_theta = rand_sample.y; 
        float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        
        float3 hemisphere_ray = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

        float scale = float(i) / float(SAMPLE_COUNT);
        scale = lerp(0.1, 1.0, scale * scale);
        hemisphere_ray *= scale;

        float3 sample_pos = vs_pos + mul(hemisphere_ray, TBN) * radius;

        float2 offset_ndc = float2(
            (sample_pos.x * proj[0][0]) / -sample_pos.z,
            (sample_pos.y * proj[1][1]) / -sample_pos.z
        );

        float2 offset_uv = offset_ndc * 0.5 + 0.5;

        if (any(offset_uv < 0.0) || any(offset_uv > 1.0)) { continue; }

        uint2 offset_coord = uint2(offset_uv * float2(dims));
        float sample_depth = gDepthTexture.Load(int3(offset_coord, 0)).x;
        
        float scene_vs_z = -proj[2][3] / sample_depth;

        float range_check = smoothstep(0.0, 1.0, radius / abs(vs_pos.z - scene_vs_z));
        if (scene_vs_z >= sample_pos.z + bias) { occlusion += 1.0 * range_check; }
    }

    float ao_factor = 1.0 - (occlusion / float(SAMPLE_COUNT));
    gOutAOImage[thread_id.xy] = float4(ao_factor.xxx, 1.0);
    //gOutAOImage[thread_id.xy] = float4(normal.xyz, 1.0);
#else
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
    const float n_samples = 8.0;    
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

			uint indirect = update_sectors(frontBackHorizon.x, frontBackHorizon.y, 0u);
			
			float stepVisibility = float(count_ones(indirect & ~occlusion) / float(N_SECTORS));
			lighting += stepVisibility * sampleLight * clamp(dot(normal, sampleHorizon), 0.0, 1.0) * clamp(dot(sampleNormal, -sampleHorizon), 0.0, 1.0);

			occlusion |= indirect;
		}
		
        visibility += 1.0 - float(count_ones(occlusion)) / float(N_SECTORS);
    }

    visibility /= n_slices;
    lighting /= n_slices;
    
    // Output AO to R Channel, and optional lighting inside GBA channels if needed
    //gOutAOImage[dtid.xy] *= float4(visibility.xxx, 1.0);
    gOutAOImage[dtid.xy] += float4(lighting.xyz, 1.0)*0.0; 
#endif
}