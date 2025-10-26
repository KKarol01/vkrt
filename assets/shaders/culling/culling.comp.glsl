#version 460

#include "./culling/common.glsli"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

bool frustum_cull(vec4 bounding_sphere)
{
    // clang-format off
    const vec4 pos = get_buf(GPUEngConstant).prev_view * vec4(bounding_sphere.xyz, 1.0);
    vec4 L = vec4(get_buf(GPUEngConstant).proj[0][3] + get_buf(GPUEngConstant).proj[0][0],
                  get_buf(GPUEngConstant).proj[1][3] + get_buf(GPUEngConstant).proj[1][0],
                  get_buf(GPUEngConstant).proj[2][3] + get_buf(GPUEngConstant).proj[2][0],
                  get_buf(GPUEngConstant).proj[3][3] + get_buf(GPUEngConstant).proj[3][0]);
    vec4 R = vec4(get_buf(GPUEngConstant).proj[0][3] - get_buf(GPUEngConstant).proj[0][0],
                  get_buf(GPUEngConstant).proj[1][3] - get_buf(GPUEngConstant).proj[1][0],
                  get_buf(GPUEngConstant).proj[2][3] - get_buf(GPUEngConstant).proj[2][0],
                  get_buf(GPUEngConstant).proj[3][3] - get_buf(GPUEngConstant).proj[3][0]);
    vec4 B = vec4(get_buf(GPUEngConstant).proj[0][3] + get_buf(GPUEngConstant).proj[0][1],
                  get_buf(GPUEngConstant).proj[1][3] + get_buf(GPUEngConstant).proj[1][1],
                  get_buf(GPUEngConstant).proj[2][3] + get_buf(GPUEngConstant).proj[2][1],
                  get_buf(GPUEngConstant).proj[3][3] + get_buf(GPUEngConstant).proj[3][1]);
    vec4 T = vec4(get_buf(GPUEngConstant).proj[0][3] - get_buf(GPUEngConstant).proj[0][1],
                  get_buf(GPUEngConstant).proj[1][3] - get_buf(GPUEngConstant).proj[1][1],
                  get_buf(GPUEngConstant).proj[2][3] - get_buf(GPUEngConstant).proj[2][1],
                  get_buf(GPUEngConstant).proj[3][3] - get_buf(GPUEngConstant).proj[3][1]);
    vec4 N = vec4(get_buf(GPUEngConstant).proj[0][3] - get_buf(GPUEngConstant).proj[0][2],
                  get_buf(GPUEngConstant).proj[1][3] - get_buf(GPUEngConstant).proj[1][2],
                  get_buf(GPUEngConstant).proj[2][3] - get_buf(GPUEngConstant).proj[2][2],
                  get_buf(GPUEngConstant).proj[3][3] - get_buf(GPUEngConstant).proj[3][2]);
 // vec4 F = vec4(get_buf(GPUEngConstant).proj[0][3] - get_buf(GPUEngConstant).proj[0][2],
 //               get_buf(GPUEngConstant).proj[1][3] - get_buf(GPUEngConstant).proj[1][2],
 //               get_buf(GPUEngConstant).proj[2][3] - get_buf(GPUEngConstant).proj[2][2],
 //               get_buf(GPUEngConstant).proj[3][3] - get_buf(GPUEngConstant).proj[3][2]);

    L /= length(L.xyz);
    R /= length(R.xyz);
    B /= length(B.xyz);
    T /= length(T.xyz);
    N /= length(N.xyz);
 // F /= length(F.xyz);

    if(    dot(L.xyz, pos.xyz) + L.w + bounding_sphere.w >= 0.0
        && dot(R.xyz, pos.xyz) + R.w + bounding_sphere.w >= 0.0
        && dot(B.xyz, pos.xyz) + B.w + bounding_sphere.w >= 0.0
        && dot(T.xyz, pos.xyz) + T.w + bounding_sphere.w >= 0.0
        && dot(N.xyz, pos.xyz) + N.w + bounding_sphere.w >= 0.0
    ) { return true; }
    return false;
    // clang-format on
}

bool project_sphere_bounds(vec3 c, float r, float znear, float P00, float P11, out vec4 aabb)
{
    if (c.z < r + znear) return false;

    vec3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    aabb = vec4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    // clip space -> uv space
    aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f);

    return true;
}

bool occlusion_cull(vec4 bounding_sphere, float P00, float P11) {
    vec3 center = vec3(get_buf(GPUEngConstant).view * vec4(bounding_sphere.xyz, 1.0));
    // -y because projection matrix does that
    // -z here, because algorithm works with -z forward, and glm view matrix waits for proj matrix to negate the z.
    float radius = bounding_sphere.w;
    center.yz *= -1.0;
	vec4 aabb;
	if (project_sphere_bounds(vec3(center.x, center.y, center.z), radius, 0.1, P00, P11, aabb))
	{
		float width = (aabb.z - aabb.x) * float(1280.0);
		float height = (aabb.w - aabb.y) * float(768.0);

		float level = max(floor(log2(max(width, height))), 1.0) - 1.0;

		float depth = textureLod(sampler2D(gt_2d[hizsrcti], g_samplers[ENG_SAMPLER_HIZ]), (aabb.xy + aabb.zw) * 0.5, level).x;

		float depthSphere = 0.1 / (center.z - radius);

		return depthSphere >= depth;
	}
    return true;
}

void main()
{
    const uint x = uint(gl_GlobalInvocationID.x);
    if(get_buf2(GPUInstanceId, srcidsbi).count <= x) { return; }

    GPUInstanceId id = get_buf2(GPUInstanceId, srcidsbi).ids_us[x];
    vec4 gpubs = get_bufb(GPUBoundingSphere, get_buf(GPUEngConstant)).bounding_spheres_us[id.resi];
    vec4 bs = get_bufb(GPUTransform, get_buf(GPUEngConstant)).transforms_us[id.insti] * vec4(gpubs.xyz, 1.0);
    bs.w = gpubs.w;

    uint first_instance = get_buf2(GPUDrawIndexedIndirectCommand, dstcmdsbi).commands_us[id.cmdi].firstInstance;
    if(occlusion_cull(bs, get_buf(GPUEngConstant).proj[0][0], get_buf(GPUEngConstant).proj[1][1]))
    {
        const uint off = atomicAdd(get_buf2(GPUDrawIndexedIndirectCommand, dstcmdsbi).commands_us[id.cmdi].instanceCount, 1);
        get_buf2(GPUInstanceId, dstidsbi).ids_us[first_instance + off] = id;
        atomicAdd(get_buf2(GPUInstanceId, dstidsbi).count, 1);
	} 
}