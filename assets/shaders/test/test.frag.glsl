#version 460

layout(location = 0) out vec4 OUT_COLOR;

layout(location = 0) in float col;

void main() {
	OUT_COLOR = vec4(vec3(1.0), 1.0);
}