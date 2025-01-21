#version 460

layout(location = 0) out vec4 OUT_COLOR;

layout(location = 0) in vec3 pos;

void main() {
	OUT_COLOR = vec4(pos, 1.0);
}