#version 460

#include "./forwardp/common.glsli"

// make 16 not hardcoded, if needed -- compile with compile constants maybe
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

vec4 plane_to_view(vec4 plane)
{
    vec2 screen_size = vec2(gl_NumWorkGroups.xy) * vec2(gl_WorkGroupSize.xy);
    plane.xy = plane.xy / screen_size;
    plane.y = 1.0 - plane.y;
    plane.xy = plane.xy * 2.0 - 1.0;
    plane = get_buf(GPUEngConstant).inv_proj * plane;
    plane /= plane.w;
    return plane;
}

GPUFWDPFrustumPlane compute_plane(vec3 p0, vec3 p1, vec3 p2)
{
    vec3 v0 = p1 - p0;
    vec3 v1 = p2 - p0;
    GPUFWDPFrustumPlane plane;
    plane.n = normalize(cross(v0, v1));
    plane.d = dot(plane.n, p0);
    return plane;
}

void main()
{
    vec4 p0 = vec4(vec2(gl_GlobalInvocationID.xy * fwdp_tile_pixels), -1.0, 1.0);
    vec4 p1 = vec4(vec2((gl_GlobalInvocationID.xy + ivec2(1, 0)) * fwdp_tile_pixels), -1.0, 1.0);
    vec4 p2 = vec4(vec2((gl_GlobalInvocationID.xy + ivec2(0, 1)) * fwdp_tile_pixels), -1.0, 1.0);
    vec4 p3 = vec4(vec2((gl_GlobalInvocationID.xy + ivec2(1, 1)) * fwdp_tile_pixels), -1.0, 1.0);
    p0 = plane_to_view(p0); // left bottom
    p1 = plane_to_view(p1); // rb
    p2 = plane_to_view(p2); // lt
    p3 = plane_to_view(p3); // rt
    vec3 eye = vec3(0.0);

    GPUFWDPFrustum f;
    f.planes[0] = compute_plane(eye, p2.xyz, p0.xyz);
    f.planes[1] = compute_plane(eye, p1.xyz, p3.xyz);
    f.planes[2] = compute_plane(eye, p0.xyz, p1.xyz);
    f.planes[3] = compute_plane(eye, p3.xyz, p2.xyz);

    const uint idx = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * gl_NumWorkGroups.x;
    get_buf(GPUFWDPFrustum).frustums_us[idx] = f;
}