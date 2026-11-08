#include "assets/shaders/ssao/ssao.shared.hlsli"
#include "assets/shaders/util.hlsli"

ENG_PUSH_CONSTANTS(GPUAOParams)

#define LOCAL_SIZE 8
#define NUM_RAYS 64

float3 get_camera_pos()
{
    return pc.engine.load().cam_pos;
}

float IGN(float2 pixel)
{
    const float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixel, magic.xy)));
}

float2 IGN2D(float2 pixel)
{
    float n1 = IGN(pixel);
    float n2 = IGN(pixel + float2(47.0, 17.0)); 
    return float2(n1, n2);
}

// Cosine-weighted hemisphere sampling
float3 sample_hemisphere(float2 xi)
{
    float phi = 2.0 * ENG_PI * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);

    return float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );
}

// Build TBN from normal
float3x3 make_tbn(float3 n)
{
    float3 up = abs(n.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return float3x3(t, b, n);
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    const GPUEngConstants consts = pc.engine.load();
    Texture2D<float> in_depth = pc.depth.get();
    Texture2D<float4> in_normal = pc.normal.get();
    RWTexture2D<float4> out_image = pc.out_ao.get();

    uint2 size;
    out_image.GetDimensions(size.x, size.y);
    if(any(thread_id.xy >= size)) return;
    if(!pc.scene.is_valid())
    {
        out_image[thread_id.xy] = float4(1.0, 1.0, 1.0, 1.0); // no TLAS: unoccluded
        return;
    }

    float2 uv = (float2(thread_id.xy) + 0.5) / float2(size);

	// 1. Reconstruct view-space position
	float depth = in_depth.SampleLevel(gSamplerStates[ENG_SAMPLER_NEAREST], uv, 0).x;
	if(depth < 0.01) 
	{ 
		out_image[thread_id.xy] = float4(0.0.xxx, 1.0);
		return; 
	}
	
    float3 pos_v = unproject_inf_revz_depth(consts.proj, float3(uv*2.0 - 1.0, depth));

    // 2. Convert to world space (gbuffer normal is already world-space)
    float3 pos_w = mul(consts.inv_view, float4(pos_v, 1)).xyz;
    float3 normal_w = normalize(in_normal.Load(int3(thread_id.xy, 0)).xyz);

    float3x3 tbn = make_tbn(normal_w);

    float2 noise = IGN2D(thread_id.xy);

    float occlusion = 0.0;

    // 4. Shoot hemisphere rays
    for(int i = 0; i < NUM_RAYS; ++i)
    {
        float2 xi = frac(noise + float2(i * 0.37, i * 0.73));
        float3 local_dir = sample_hemisphere(xi);
        float3 ray_dir = mul(local_dir, tbn);

        RayDesc ray;
        ray.Origin    = pos_w;
        ray.Direction = ray_dir;
        ray.TMin      = pc.ao.bias;
        ray.TMax      = pc.ao.radius;

        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(pc.scene.get(), RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);

        q.Proceed();

        if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            occlusion += 1.0;
        }
    }

    float ao = 1.0 - (occlusion / NUM_RAYS);

    out_image[thread_id.xy] = float4(ao.xxx, 1.0);
}