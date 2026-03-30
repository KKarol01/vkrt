#include "./common.hlsli"
#include "./assets/shaders/util.hlsli"

static const uint LOCAL_SIZE = 8;
static const float RADIUS = 0.5;
static const float BIAS = 0.1;

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
	
    const float2 uv = (float2(thread_id.xy) + 0.5) / float2(dims);
    float depth = in_depth.Load(int3(thread_id.xy, 0)).x; 
	
    // Reverse-Z: 0.0 is infinity, 1.0 is near. 
    // If depth is near 0, it's the skybox/far plane.
    if(depth < 1e-5) { out_ao[thread_id.xy] = 0.0; return; }  
	
    const float3 vs_pos = unproject_inf_revz_depth(float3(uv * 2.0 - 1.0, depth));
    const float3 vs_normal = normalize(in_normal[thread_id.xy].xyz);
     
    const float3 random_vec = normalize(hash32(uv) * 2.0 - 1.0);
    const float3 tangent = normalize(random_vec - vs_normal * dot(random_vec, vs_normal));
    const float3 bitangent = cross(vs_normal, tangent);
    
    // Construct TBN as rows: [tangent, bitangent, normal]
    // To transform from Tangent -> View using mul(vec, TBN)
    const float3x3 TBN = float3x3(tangent, bitangent, vs_normal); 
	
	float occlusion = 0.0;
    const float4x4 proj = get_gsb(GPUEngConstants, 0).proj;

    for(uint i=0; i<64; ++i)
    {
        // 1. Ensure sample is pushed OUTWARD from the surface
        // If vs_normal.z is negative in your view space, 
        // you might need to flip the Z of the sample.
        float3 sample_vs_offset = mul(TBN, SSAO_SAMPLES[i].xyz);
        float3 sample_pos = vs_pos + sample_vs_offset * RADIUS;
		
        // 2. Project back to screen (Vector * Matrix)
        float4 offset = mul(proj, float4(sample_pos, 1.0));
        float2 sample_ndc = offset.xy / offset.w;
        float2 sample_uv = sample_ndc * 0.5 + 0.5;
        
        // 3. Sample the depth buffer
        const float sampled_z_depth = in_depth.SampleLevel(gSamplerStates[ENG_SAMPLER_NEAREST], sample_uv, 0);
        
        // Skip background/skybox samples
        if(sampled_z_depth < 1e-5) continue;

        const float3 actual_vs_pos = unproject_inf_revz_depth(float3(sample_ndc, sampled_z_depth));
		
        // 4. RANGE CHECK: If the depth difference is too large, it's a different object
        float dist_diff = abs(vs_pos.z - actual_vs_pos.z);
        float range_check = smoothstep(0.0, 1.0, RADIUS / dist_diff);
        
        // 5. OCCLUSION LOGIC: 
        // If geometry (actual) is closer to camera than the sample, it's occluded.
        // Try flipping '<' to '>' if your Z-axis is inverted.
        if (actual_vs_pos.z > (sample_pos.z + BIAS)) {
            occlusion += range_check;
        }
    }
    out_ao[thread_id.xy] = (1.0 - (occlusion / 64.0)) * in_normal[thread_id.xy];
}