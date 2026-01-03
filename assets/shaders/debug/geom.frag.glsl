#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) out vec4 OUT_COLOR;

void main() 
{
	OUT_COLOR = vec4(1.0, 0.0, 0.0, 1.0);
}