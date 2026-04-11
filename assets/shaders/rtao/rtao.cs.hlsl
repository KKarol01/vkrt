#include "./common.hlsli"
#include "./assets/shaders/util.hlsli"

#define LOCAL_SIZE 8

float3 get_camera_pos()
{
	return get_gsb(GPUEngConstants, 0).cam_pos;
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
	RWTexture2D<float4> out_image = gRWTexture2Df4s[pc.AOImageIndex];
    
    uint2 out_size;
    out_image.GetDimensions(out_size.x, out_size.y);
    if(any(thread_id.xy >= out_size)) return;

    // 1. Calculate Ray Direction
    const float2 uv = (float2(thread_id.xy) + 0.5) / float2(out_size) * 2.0 - 1.0;
    const float3 ray_origin = get_camera_pos(); 
    // Unprojecting UV to a point in world space to find the direction vector
    const float3 world_pos = mul(get_gsb(GPUEngConstants, 0).inv_view,  float4(unproject_inf_revz_depth(float3(uv, 1.0)), 1.0)).xyz; 
    const float3 ray_dir = normalize(world_pos - ray_origin);

    // 2. Define Ray
    RayDesc ray;
    ray.Origin    = ray_origin;
    ray.Direction = ray_dir;
    ray.TMin      = 0.001;
    ray.TMax      = 1000.0;

    // 3. Trace Geometry
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(gTLASs[pc.SceneTlasIndex], RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);
    q.Proceed();

    // 4. Write Result
    float depth = 0.0;
    if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        depth = q.CommittedRayT();
    }

    out_image[thread_id.xy] = float4(depth.xxx, 1.0);
}