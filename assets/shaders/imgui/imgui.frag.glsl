#version 460
#include "./imgui/common.inc.glsl"

layout(location = 0) in struct { vec4 color; vec2 uv; } In;

layout(location = 0) out vec4 OUT_COLOR;
void main()
{
	OUT_COLOR = In.color * texture(imgui_texture, In.uv);
}