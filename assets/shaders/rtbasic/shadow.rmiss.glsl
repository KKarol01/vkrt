#version 460
#extension GL_EXT_ray_tracing : enable

layout(location = 1) rayPayloadInEXT struct RayPayloadShadow {
    float distance;
} payload_shadow;

void main() {
    const vec2 d = gl_WorldRayDirectionEXT.xy;

    payload_shadow.distance = 1000.0;
}