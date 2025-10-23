#version 460

#include "./forwardp/common.glsli"

// make 16 not hardcoded, if needed -- compile with compile constants maybe
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

vec3 plane_to_view(vec4 plane)
{
    vec2 screen_size = vec2(gl_NumWorkGroups.xy) * vec2(gl_WorkGroupSize.xy);
    plane.xy = plane.xy / vec2(1280.0, 768.0);
    plane.x = plane.x * 2.0 - 1.0;
    plane.y = 1.0 - plane.y * 2.0;
    plane = get_buf(GPUEngConstant).inv_proj * plane;
    plane.xyz /= plane.w;
    return plane.xyz;
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
    return;

    vec4 p0 = vec4(vec2(gl_GlobalInvocationID.xy * 16), 1.0, 1.0);
    vec4 p1 = vec4(vec2((gl_GlobalInvocationID.xy + ivec2(1, 0)) * 16), 1.0, 1.0);
    vec4 p2 = vec4(vec2((gl_GlobalInvocationID.xy + ivec2(0, 1)) * 16), 1.0, 1.0);
    vec4 p3 = vec4(vec2((gl_GlobalInvocationID.xy + ivec2(1, 1)) * 16), 1.0, 1.0);
    vec3 v0 = plane_to_view(p0); // left bottom
    vec3 v1 = plane_to_view(p1); // rb
    vec3 v2 = plane_to_view(p2); // lt
    vec3 v3 = plane_to_view(p3); // rt
    vec3 eye = vec3(0.0);

    GPUFWDPFrustum f;
    f.planes[0] = compute_plane(eye, v2, v0); // left
    f.planes[1] = compute_plane(eye, v1, v3); // right
    f.planes[2] = compute_plane(eye, v0, v1); // bottom
    f.planes[3] = compute_plane(eye, v3, v2); // top

    const uint idx = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * 80;
    // todo: hardcoded grid resolution
    if(
        gl_GlobalInvocationID.x < 80 && 
        gl_GlobalInvocationID.y < 48
    ) {
        get_buf(GPUFWDPFrustum).frustums_us[idx] = f; 
    }
}