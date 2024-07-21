#version 460
#extension GL_EXT_ray_tracing : enable

#include "ray_payload.inc"

void main() {
    const vec2 d = gl_WorldRayDirectionEXT.xy;

    payload.radiance = vec3(0.0);
    payload.distance = 1000.0;
}