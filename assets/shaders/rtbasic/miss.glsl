#version 460
#extension GL_EXT_ray_tracing : enable

#include "ray_payload.inc"

void main() {
    payload.radiance = vec3(0.3, 0.4, 0.6) * 0.0;
    payload.distance = 1000.0;
    payload.albedo = vec3(0.0);
    payload.normal = vec3(0.0);
}