#version 460
#include "./imgui/common.inc.glsl"

layout(location = 0) in struct { vec4 color; vec2 uv; } In;

layout(location = 0) out vec4 OUT_COLOR;
void main()
{
	OUT_COLOR = In.color * texture(sampler2D(imgui_texture, g_samplers[G_SAMPLER_LINEAR]), In.uv);
}