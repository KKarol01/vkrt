#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    flat uint32_t instance_index;
    vec3 water_normal;
    flat vec2 aabb_center;
}
vsout;

bool projectSphere(vec3 C, float r, float znear, float P00, float P11, out vec4 aabb)
{
//	if (C.z < r + znear)
//		return false;

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

void main() {
    vec3 pos = vertex_pos_arr[gl_VertexIndex];
    vec4 vpos = constants.proj_view * vec4(pos, 1.0);

    GPUInstanceId id = meshlet_ids[culled_ids[gl_InstanceIndex]];
    vec4 bs = meshlets_bs[culled_ids[gl_InstanceIndex]];

    vec4 view_bs = constants.view * vec4(bs.xyz, 1.0);
    //view_bs.y *= -1.0;
    view_bs.z *= -1.0;
    vec4 aabb;
    projectSphere(view_bs.xyz, bs.w, 0.1, constants.proj[0][0], constants.proj[1][1], aabb);
    vsout.aabb_center = vec2(aabb.xy + aabb.zw) * 0.5;

    vsout.instance_index = uint(culled_ids[gl_InstanceIndex] * 0x6F7DEF7);
    gl_Position = vpos;

    // vsout.position = vec3(transforms_arr[gl_InstanceIndex] * vec4(vertex_pos_arr[gl_VertexIndex], 1.0));
    // vsout.normal = attrib_pos_arr[gl_VertexIndex].normal;
    // vsout.uv = attrib_pos_arr[gl_VertexIndex].uv;
    // vsout.tangent = attrib_pos_arr[gl_VertexIndex].tangent.xyz * attrib_pos_arr[gl_VertexIndex].tangent.w;
    // vsout.instance_index = gl_InstanceIndex;
    // vec3 disp = textureLod(combinedImages_2d[fft_displacement_index], vsout.uv, 0.0).rgb;
    // vsout.position += disp;
    // vsout.water_normal = disp;
    // gl_Position = constants.proj_view * vec4(vsout.position, 1.0);
}