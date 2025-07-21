#version 460

#include "./culling/common.inc.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

bool frustum_cull(vec4 bounding_sphere)
{
    // clang-format off
    const vec4 pos = constants.view * vec4(bounding_sphere.xyz, 1.0);
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
    vec4 N = vec4(constants.proj[0][2],
                  constants.proj[1][2],
                  constants.proj[2][2],
                  constants.proj[3][2]);
    vec4 F = vec4(constants.proj[0][3] - constants.proj[0][2],
                  constants.proj[1][3] - constants.proj[1][2],
                  constants.proj[2][3] - constants.proj[2][2],
                  constants.proj[3][3] - constants.proj[3][2]);

    L /= length(L.xyz);
    R /= length(R.xyz);
    B /= length(B.xyz);
    T /= length(T.xyz);
    N /= length(N.xyz);
    F /= length(F.xyz);

    if(    dot(L.xyz, pos.xyz) + L.w + bounding_sphere.w >= 0.0
        && dot(R.xyz, pos.xyz) + R.w + bounding_sphere.w >= 0.0
        && dot(B.xyz, pos.xyz) + B.w + bounding_sphere.w >= 0.0
        && dot(T.xyz, pos.xyz) + T.w + bounding_sphere.w >= 0.0
        && dot(N.xyz, pos.xyz) + N.w + bounding_sphere.w >= 0.0
        && dot(F.xyz, pos.xyz) + F.w + bounding_sphere.w >= 0.0) { return true; }
    return false;
    // clang-format on
}

bool projectSphere(vec3 C, float r, float znear, float P00, float P11, out vec4 aabb)
{
	if (C.z < r + znear)
		return false;

	vec2 cx = -C.xz;
	vec2 vx = vec2(sqrt(dot(cx, cx) - r * r), r);
	vec2 minx = mat2(vx.x, vx.y, -vx.y, vx.x) * cx;
	vec2 maxx = mat2(vx.x, -vx.y, vx.y, vx.x) * cx;

	vec2 cy = -C.yz;
	vec2 vy = vec2(sqrt(dot(cy, cy) - r * r), r);
	vec2 miny = mat2(vy.x, vy.y, -vy.y, vy.x) * cy;
	vec2 maxy = mat2(vy.x, -vy.y, vy.y, vy.x) * cy;

	aabb = vec4(minx.x / minx.y * P00, miny.x / miny.y * P11, maxx.x / maxx.y * P00, maxy.x / maxy.y * P11);
	aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f); // clip space -> uv space

	return true;
}

bool occlusion_cull(vec4 bounding_sphere, float P00, float P11) {
//return true;
    vec3 center = vec3(constants.debug_view * vec4(bounding_sphere.xyz, 1.0));
    float radius = bounding_sphere.w * 0.5;
    //center.y *= -1.0;
    center.z *= -1.0;
	vec4 aabb;
	if (projectSphere(center, radius, 0.1, P00, P11, aabb))
	{
		float width = (aabb.z - aabb.x) * float(textureSize(hiz_source, 0).x);
		float height = (aabb.w - aabb.y) * float(textureSize(hiz_source, 0).y);

		float level = floor(log2(max(width, height)));

		// Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
		float depth = textureLod(hiz_source, (aabb.xy + aabb.zw) * 0.5, level).x;

        //center.y *= -1.0;
        center.z *= -1.0;
        vec4 proj_sphere = constants.proj * vec4(center.xy, center.z + radius, 1.0);
        proj_sphere /= proj_sphere.w;
		float depthSphere = proj_sphere.z;
		return depthSphere <= depth;
	}
    return false;
}

void main()
{
    const uint x = uint(gl_GlobalInvocationID.x);
    if(instance_ids.count <= x) { return; }

    GPUInstanceId id = instance_ids.ids_us[x];

    const vec4 bs = instance_bs[x];

    if(frustum_cull(bs) && occlusion_cull(bs, constants.proj[0][0], constants.proj[1][1]))
    {
        const uint off = atomicAdd(indirect_cmds.commands_us[id.batch_id].instanceCount, 1);
        post_cull_instance_ids[indirect_cmds.commands_us[id.batch_id].firstInstance + off] = x;
    }
}