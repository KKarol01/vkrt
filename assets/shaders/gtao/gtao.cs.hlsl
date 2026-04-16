#include "./common.hlsli"
#include "./assets/shaders/common.hlsli"
#include "./assets/shaders/util.hlsli"

#define LOCAL_SIZE 8

float3 project_on_plane(float3 a, float3 b)
{
	return a - b*dot(a, b); // subtracts orthogonal component, moving 
							// sticking out vector back onto a plane defined by 'a'.
}

float IGN(float2 pixel)
{
    const float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel, magic.xy)));
}

float2 IGN2D(float2 pixel)
{
    float n1 = IGN(pixel);
    // Offset by a value that breaks the grid pattern
    float n2 = IGN(pixel + float2(47.0, 17.0)); 
    return float2(n1, n2);
}

float GTAO(
    Texture2D<float> depthTex,
    float3 normal_v,
    SamplerState samp,
    float2 uv,
	float2 noise
) 
{
    const float NUM_SAMPLES = 4.0;
    const float NUM_DIRECTIONS = 4.0;
	float3 pos_v = unproject_inf_revz_depth(uv, depthTex); 
	float3 view_v = normalize(-pos_v);
	
    float2 scaling = float2(
		get_gsb(GPUEngConstants, 0).proj[0][0],
		get_gsb(GPUEngConstants, 0).proj[1][1]) / -pos_v.z;
	//noise.xy *= scaling;
	scaling *= get_gsb(GPUEngAOSettings, 0).radius;
	
	
	float visibility = 0.0;
	float correction = 0.0;
	for(float i=0.0; i < NUM_DIRECTIONS; ++i) 
	{
		float phi = ENG_PI / NUM_DIRECTIONS * (i + noise.x); 
		float2 omega = float2(cos(phi), sin(phi));
		
		float3 dir_v = float3(omega, 0);
		float3 ortho_dir_v = project_on_plane(dir_v, view_v);
		float3 axis_v = normalize(cross(dir_v, view_v));
		float3 proj_norm_v = project_on_plane(normal_v, axis_v);
		float weight = length(proj_norm_v);
		
		float sgn_n = sign(dot(ortho_dir_v, proj_norm_v));
		float cos_n = saturate(dot(proj_norm_v, view_v) / weight);
		float n = sgn_n * acos(cos_n); 
		
		[unroll]
		for(float side = 0.0; side < 2; ++side)
		{
			float horizon_cos_angle = -1.0;
			[unroll]
			for(float k=0.0; k < NUM_SAMPLES; ++k)
			{
				float s = (k + noise.y) / NUM_SAMPLES;
				float2 sample_uv = uv + (side * 2.0 - 1.0) * s * scaling * float2(omega.x, omega.y);
				if (any(sample_uv < 0) || any(sample_uv > 1)) break;
				float3 sample_pos = unproject_inf_revz_depth(sample_uv, depthTex);
				float3 sample_horizon = normalize(sample_pos - pos_v);
				if(distance(sample_pos, pos_v) < 0.01) { continue; }
				horizon_cos_angle = max(horizon_cos_angle, dot(sample_horizon, view_v));
			}
			float h = n + clamp((side * 2.0 - 1.0)*acos(horizon_cos_angle) - n, -ENG_HALF_PI, ENG_HALF_PI);
			visibility += weight * (cos_n + 2.0*h*sin(n) - cos(2.0*h - n)) / 4.0;
		}
		
		correction += weight * (n*sin(n) + cos_n);
	}
	
	return visibility / correction;
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    Texture2D<float> in_depth = gTexture2Df1s[pc.DepthTextureIndex];
	RWTexture2D<float4> in_normal = gRWTexture2Df4s[pc.NormalImageIndex];
	Texture2D<float4> in_noise = gTexture2Ds[pc.NoiseTextureIndex];
	RWTexture2D<float4> out_ao = gRWTexture2Df4s[pc.AOImageIndex];
	
	uint2 out_size;
	out_ao.GetDimensions(out_size.x, out_size.y);
	if(any(thread_id.xy >= out_size)) { return; }
	
	const float2 uv = float2(thread_id.xy + 0.5) / float2(out_size.xy);
	const float3 normal_v = in_normal[thread_id.xy].xyz;
	const float3 pos_v = unproject_inf_revz_depth(uv, in_depth);
	float2 noise = IGN2D(thread_id.xy); 
	const float gtao = GTAO(in_depth, in_normal[thread_id.xy].xyz, gSamplerStates[ENG_SAMPLER_NEAREST], uv, noise);
	
	out_ao[thread_id.xy] = float4(gtao.xxx, 1.0);
} 