#include "./common.hlsli"
#include "./assets/shaders/util.hlsli"

static const uint LOCAL_SIZE = 8;

float rand2dTo1d(float2 value, float2 dotDir = float2(12.9898, 78.233))
{
	float2 smallValue = sin(value);
	float random = dot(smallValue, dotDir);
	random = frac(sin(random) * 143758.5453);
	return random;
}
float3 hash32(float2 value) 
{
	return float3(
		rand2dTo1d(value, float2(12.989, 78.233)),
		rand2dTo1d(value, float2(39.346, 11.135)),
		rand2dTo1d(value, float2(73.156, 52.235))
	);
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    Texture2D<float> in_depth = gTexture2Df1s[pc.DepthTextureIndex];
    RWTexture2D<float4> in_normal = gRWTexture2Df4s[pc.NormalImageIndex];
    RWTexture2D<float4> out_ao = gRWTexture2Df4s[pc.AOImageIndex];
	
    uint2 dims;
    out_ao.GetDimensions(dims.x, dims.y);
    if(any(thread_id.xy >= dims.xy)) { return; } 
	
	//float normll = in_normal[thread_id.xy].z;
	//normll *= -1.0;
	//out_ao[thread_id.xy] = normll;
	//return;
	
	GPUEngAOSettings in_ao_settings = get_gsb(GPUEngAOSettings, 0);
	
    const float2 uv = (float2(thread_id.xy) + 0.5) / float2(dims);
    float depth = in_depth.Load(int3(thread_id.xy, 0)).x; 
	
    // Reverse-Z: 0.0 is infinity, 1.0 is near. 
    // If depth is near 0, it's the skybox/far plane.
    if(depth < 1e-5) { out_ao[thread_id.xy] = 0.0; return; }  
	
    const float3 vs_pos = unproject_inf_revz_depth(float3(uv * 2.0 - 1.0, depth));
	const float3 vs_normal = (in_normal[thread_id.xy].xyz);

    const float3 random_vec = gTexture2Ds[pc.NoiseTextureIndex].SampleLevel(gSamplerStates[ENG_SAMPLER_NEAREST], uv * (float2(dims.xy) / 4.0), 0).xyz;
    const float3 tangent = normalize(random_vec + vs_normal * dot(random_vec, vs_normal));
    const float3 bitangent = cross(vs_normal, tangent); 
    
    const float3x3 TBN = float3x3(tangent, bitangent, vs_normal); 
	
	float occlusion = 0.0;
    const float4x4 proj = get_gsb(GPUEngConstants, 0).proj;

    for(uint i=0; i<64; ++i)
    {
        // multiply from the right, because this matrix was constructed
		// inside the shader, which means it's row-major, contrary
		// to matrices sent from the CPU (which were made using GLM)
		// that need to be multiplied from the left :).
        float3 sample_vs_offset = mul(gRWBuffers[pc.SampleBufferIndex].Load<float3>(i * sizeof(float3)).xyz, TBN);
		// sample has to have offset subtracted, not added, for some reason.
        float3 sample_pos = vs_pos + sample_vs_offset * in_ao_settings.radius;
		
        float4 offset = mul(proj, float4(sample_pos, 1.0));
        float2 sample_ndc = offset.xy / offset.w;
        float2 sample_uv = sample_ndc * 0.5 + 0.5;
		
		if(any(sample_uv < 0.0) || any(sample_uv > 1.0)) continue;
        
		uint2 sample_px = uint2(sample_uv * float2(dims));
		sample_px = clamp(sample_px, uint2(0, 0), dims - 1);

		float sampled_z_depth = in_depth.Load(int3(sample_px, 0));
        
        if(sampled_z_depth < 1e-5) continue;

        const float3 actual_vs_pos = unproject_inf_revz_depth(float3(sample_uv * 2.0 - 1.0, sampled_z_depth));
		
        float dist_diff = abs(vs_pos.z - actual_vs_pos.z);
        float range_check = saturate(1.0 - abs(vs_pos.z - actual_vs_pos.z) / in_ao_settings.radius);
        
        if (actual_vs_pos.z >= sample_pos.z + in_ao_settings.bias) { occlusion += range_check; }
    }
    out_ao[thread_id.xy] = 1.0 - (occlusion / 64.0);
}