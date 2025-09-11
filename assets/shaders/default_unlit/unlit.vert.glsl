#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    flat uint32_t instance_index;
} vsout;

void main() 
{
    vec3 pos = engvpos[gl_VertexIndex];

    GPUInstanceId id = get_id(gl_InstanceIndex);
    gl_Position = engconsts.proj_view * get_trs(id.resource_id) * vec4(pos, 1.0);
    vsout.position = gl_Position.xyz;
    vsout.instance_index = id.batch_id * 0x6F7DEF7;
}