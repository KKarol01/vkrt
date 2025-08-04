#version 460
#include "./imgui/common.inc.glsl"

layout(location = 0) out struct { vec4 color; vec2 uv; } Out;

void main()
{
	ImGuiVertex vx = imgui_vertices[gl_VertexIndex];
	Out.color = vx.color;
	Out.uv = vx.uv;
	gl_Position = vec4(vx.pos * scale + translate, 0.0, 1.0);
}