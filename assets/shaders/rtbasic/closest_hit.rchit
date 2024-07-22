#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "ray_payload.inc"
#include "push_constants.inc"
#include "light.inc"

layout(binding = 4, set = 0) uniform sampler2D textures[];

hitAttributeEXT vec2 barycentric_weights;

void main()
{
  uint32_t triangle_offset = combined_rt_buffs.offsets.offsets[gl_InstanceID];
  uint32_t mesh_id = combined_rt_buffs.mesh_ids.ids[triangle_offset + gl_PrimitiveID];
  MeshData mesh_data = combined_rt_buffs.meshes.mesh_datas[mesh_id];

  const uint32_t i0 = index_buffer.indices[(triangle_offset + gl_PrimitiveID)*3 + 0];
  const uint32_t i1 = index_buffer.indices[(triangle_offset + gl_PrimitiveID)*3 + 1];
  const uint32_t i2 = index_buffer.indices[(triangle_offset + gl_PrimitiveID)*3 + 2];
  const Vertex v0 = vertex_buffer.vertices[i0];
  const Vertex v1 = vertex_buffer.vertices[i1];
  const Vertex v2 = vertex_buffer.vertices[i2];

  const float b = barycentric_weights.x;
  const float c = barycentric_weights.y;
  const float a = 1.0 - b - c;

  const vec3 pos = v0.pos * a + v1.pos * b + v2.pos * c;
  const vec2 uvs = v0.uv * a + v1.uv * b + v2.uv * c;
  const vec3 nor = v0.nor * a + v1.nor * b + v2.nor * c;
  const vec3 color_value = texture(textures[nonuniformEXT(mesh_data.color_texture)], uvs).rgb;
 
  payload.radiance = color_value * calc_direct_lighting(pos, nor);
  payload.distance = gl_RayTmaxEXT;
}

/*
    gl_InstanceID - instance of BLAS in TLAS
    gl_GeometryIndexEXT - idx of geometry inside one BLAS (currently it's always 0)
    gl_PrimitiveID - local for each geometry inside BLAS
*/