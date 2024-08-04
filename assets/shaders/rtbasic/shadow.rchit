#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "push_constants.inc"
#include "descriptor_layout.inc"
#include "light.inc"
#include "common.inc"
#include "probes.inc.glsl"

layout(location = 1) rayPayloadInEXT struct RayPayloadShadow {
    float distance;
} payload_shadow;

hitAttributeEXT vec2 barycentric_weights;

void main()
{
  payload_shadow.distance = gl_HitTEXT;
}

/*
    gl_InstanceID - instance of BLAS in TLAS
    gl_GeometryIndexEXT - idx of geometry inside one BLAS (currently it's always 0)
    gl_PrimitiveID - local for each geometry inside BLAS
*/