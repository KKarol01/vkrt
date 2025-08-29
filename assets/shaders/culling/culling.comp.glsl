#version 460

#include "./culling/common.inc.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

bool frustum_cull(vec4 bounding_sphere)
{
    // clang-format off
    const vec4 pos = constants.debug_view * vec4(bounding_sphere.xyz, 1.0);
    vec4 L = vec4(constants.proj[0][3] + constants.proj[0][0],
                  constants.proj[1][3] + constants.proj[1][0],
                  constants.proj[2][3] + constants.proj[2][0],
                  constants.proj[3][3] + constants.proj[3][0]);
    vec4 R = vec4(constants.proj[0][3] - constants.proj[0][0],
                  constants.proj[1][3] - constants.proj[1][0],
                  constants.proj[2][3] - constants.proj[2][0],
                  constants.proj[3][3] - constants.proj[3][0]);
    vec4 B = vec4(constants.proj[0][3] + constants.proj[0][1],
                  constants.proj[1][3] + constants.proj[1][1],
                  constants.proj[2][3] + constants.proj[2][1],
                  constants.proj[3][3] + constants.proj[3][1]);
    vec4 T = vec4(constants.proj[0][3] - constants.proj[0][1],
                  constants.proj[1][3] - constants.proj[1][1],
                  constants.proj[2][3] - constants.proj[2][1],
                  constants.proj[3][3] - constants.proj[3][1]);
//    vec4 N = vec4(constants.proj[0][2],
//                  constants.proj[1][2],
//                  constants.proj[2][2],
//                  constants.proj[3][2]);
//    vec4 F = vec4(constants.proj[0][3] - constants.proj[0][2],
//                  constants.proj[1][3] - constants.proj[1][2],
//                  constants.proj[2][3] - constants.proj[2][2],
//                  constants.proj[3][3] - constants.proj[3][2]);

    L /= length(L.xyz);
    R /= length(R.xyz);
    B /= length(B.xyz);
    T /= length(T.xyz);
  //  N /= length(N.xyz);
  //  F /= length(F.xyz);

    if(    dot(L.xyz, pos.xyz) + L.w + bounding_sphere.w >= 0.0
        && dot(R.xyz, pos.xyz) + R.w + bounding_sphere.w >= 0.0
        && dot(B.xyz, pos.xyz) + B.w + bounding_sphere.w >= 0.0
        && dot(T.xyz, pos.xyz) + T.w + bounding_sphere.w >= 0.0) { return true; }
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
    vec3 center = vec3(constants.debug_view * vec4(bounding_sphere.xyz, 1.0));
    center.y *= -1.0;
    float radius = max(bounding_sphere.w, 0.4) * 1.2;
	vec4 aabb;
	if (project_sphere_bounds(vec3(center.xy, -center.z), radius, 0.1, P00, P11, aabb))
	{
		float width = (aabb.z - aabb.x) * float(textureSize(hiz_source, 0).x);
		float height = (aabb.w - aabb.y) * float(textureSize(hiz_source, 0).y);

		float level = floor(log2(max(width, height)));

		float depth = textureLod(hiz_source, (aabb.xy + aabb.zw) * 0.5, level).x;

		float depthSphere = 0.1 / (-center.z - radius);
		return depthSphere >= depth;
	}
    return true;
}

void main()
{
    const uint x = uint(gl_GlobalInvocationID.x);
    if(instance_ids.count <= x) { return; }

    GPUInstanceId id = instance_ids.ids_us[x];
    vec4 bs = vec4(vec3(transforms[id.resource_id] * vec4(instance_bs[x].xyz, 1.0)), instance_bs[x].w);

    if(frustum_cull(bs) && occlusion_cull(bs, constants.proj[0][0], constants.proj[1][1]))
    {
        const uint off = atomicAdd(indirect_cmds.commands_us[id.batch_id].instanceCount, 1);
        atomicAdd(indirect_cmds.post_cull_triangle_count, indirect_cmds.commands_us[id.batch_id].indexCount / 3);
        post_cull_instance_ids[indirect_cmds.commands_us[id.batch_id].firstInstance + off] = x;
    }
}