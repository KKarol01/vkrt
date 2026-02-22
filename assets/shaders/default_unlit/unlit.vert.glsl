#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out VsOut {
    vec3 position;
    // vec3 normal;
    // vec2 uv;
    // flat uint32_t iidx;
} vsout;

void main() 
{
	vec3 pos = get_buf(GPUVertexPosition).positions_us[gl_VertexIndex];
	vsout.position = pos;
	
	gl_Position = get_buf(GPUEngConstant).proj_view * vec4(vsout.position, 1.0);

    // vec3 pos = engvpos[gl_VertexIndex];
	// GPUInstanceId id = get_id(gl_InstanceIndex);
	// vsout.position = vec3(get_trs(id.insti) * vec4(pos, 1.0));
	// gl_Position = get_buf(GPUEngConstant).proj_view * vec4(vsout.position, 1.0);
	// vsout.normal = engvattrs[gl_VertexIndex].normal;
	// vsout.uv = engvattrs[gl_VertexIndex].uv;
	// vsout.iidx = gl_InstanceIndex;
}