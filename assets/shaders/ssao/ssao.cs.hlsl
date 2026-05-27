#include "common.hlsli"

static const uint LOCAL_SIZE = 8;

float3 hash3df(float2 value)
{
	float3 p3 = frac(float3(value.xyx) * float3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yxz+33.33);
	return frac((p3.xxy+p3.yzz)*p3.zyx);
}

static const uint SAMPLE_COUNT = 64;

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 dims;
    gOutAOImage.GetDimensions(dims.x, dims.y);
    if (any(thread_id.xy >= dims.xy)) { return; }

    const float2 uv = (float2(thread_id.xy) + 0.5) / float2(dims);
    float depth = gDepthTexture.Load(int3(thread_id.xy, 0)).x; 

    if (depth < 1e-5) 
    { 
        gOutAOImage[thread_id.xy] = float4(1.0, 1.0, 1.0, 1.0); 
        return; 
    }

    //GPUEngAOSettings in_ao_settings = get_gsb(GPUEngAOSettings, 0);
    const float radius = 0.8; //in_ao_settings.Radius;
    const float bias = 0.01; //in_ao_settings.Bias;

    float3 vs_pos = depth_to_view_pos(thread_id.xy, dims, depth);
    float3 normal = calculate_normal_from_depth(int2(thread_id.xy), int2(dims), gDepthTexture);

    float2 noise_scale = float2(dims) / 4.0; 
    float3 random_vec = float3(gNoiseTexture.SampleLevel(gSamplerStates[ENG_SAMPLER_LINEAR], uv * noise_scale, 0).rg, 0.0);

    float3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, normal);

    const float4x4 proj = get_gsb(GPUEngConstants, 0).proj;
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
    gOutAOImage[thread_id.xy] *= float4(ao_factor.xxx, 1.0);
    //gOutAOImage[thread_id.xy] = float4(normal.xyz, 1.0);
}