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

vec4 project_sphere_bounds(vec3 c, float r, float P00, float P11)
{
    vec3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    vec4 aabb = vec4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    aabb = aabb.xyzw * vec4(0.5f, 0.5f, 0.5f, 0.5f) + vec4(0.5f);
    return vec4(
      min(aabb.x, aabb.z),
      min(aabb.y, aabb.w),
      max(aabb.x, aabb.z),
      max(aabb.y, aabb.w)
    );
}

bool occlusion_cull(vec4 bounding_sphere, float P00, float P11)
{
    vec3 center = bounding_sphere.xyz;
    float radius = bounding_sphere.w * 1.2;
    center = vec3(constants.view * vec4(center.xyz, 1.0));
    vec4 aabb = project_sphere_bounds(center, radius, constants.proj[0][0], constants.proj[1][1]);

	float width = (aabb.z - aabb.x) * textureSize(hiz_source, 0).x;
	float height = (aabb.w - aabb.y) * textureSize(hiz_source, 0).y;

	//find the mipmap level that will match the screen size of the sphere
	float level = floor(log2(max(width, height)));

	//sample the depth pyramid at that specific level
	float depth = textureLod(hiz_source, (aabb.xy + aabb.zw) * 0.5, level).x;

    center.z -= radius;
    vec4 proj_center = constants.proj * vec4(center, 1.0);
	float depthSphere = proj_center.z / proj_center.w;

	//if the depth of the sphere is in front of the depth pyramid value, then the object is visible
    imageStore(hiz_debug, ivec2((aabb.xy + aabb.zw) * 0.5 * vec2(imageSize(hiz_debug).xy)), vec4(depth, depthSphere, 0.0, 0.0));

    return true;
    return depthSphere <= depth;
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