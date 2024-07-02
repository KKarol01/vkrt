#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main() {
    const vec2 d = gl_WorldRayDirectionEXT.xy;

    hitValue = vec3(d.x, d.y, 0.2);
}