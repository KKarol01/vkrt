#version 460

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nor;
layout(location = 2) in vec2 uv;

void main() {
	const vec3 verts[] = {
		vec3(0.0, 0.0, 0.0),
		vec3(1.0, 0.0, 0.0),
		vec3(0.0, 1.0, 0.0)
	};
	gl_Position = vec4(verts[gl_VertexIndex], 1.0);
}