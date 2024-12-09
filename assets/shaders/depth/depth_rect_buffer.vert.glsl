#version 460 core

layout(location = 0) out VsOut {
	vec3 pos;	
} vs_out;

const vec3 verts[] = vec3[](
	vec3(-1.0, -1.0, 0.0),
	vec3( 1.0, -1.0, 0.0),
	vec3(-1.0,  1.0, 0.0),
	vec3(-1.0,  1.0, 0.0),
	vec3( 1.0, -1.0, 0.0),
	vec3( 1.0,  1.0, 0.0)
);

void main() {
	vs_out.pos = verts[gl_VertexIndex];
	vs_out.pos.y *= -1.0;

	gl_Position = vec4(verts[gl_VertexIndex], 1.0);
}