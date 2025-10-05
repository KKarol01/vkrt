#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    vec2 uv;
    flat uint32_t iidx;
} vsout;

void main() 
{
    vec3 pos = engvpos[gl_VertexIndex];

    GPUInstanceId id = get_id(gl_InstanceIndex);
    gl_Position = engconsts.proj_view * get_trs(id.instidx) * vec4(pos, 1.0);
    vsout.position = gl_Position.xyz;
    vsout.uv = engvattrs[gl_VertexIndex].uv;
    vsout.iidx = gl_InstanceIndex;
}