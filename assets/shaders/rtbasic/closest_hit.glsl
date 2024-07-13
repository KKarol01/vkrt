#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(binding = 3, set = 0) uniform sampler2D textures[];

struct Vertex {
    vec3 pos;
    vec3 nor;
    vec2 uv;
};

layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer PerTriangleMaterialIds {
    uint32_t ids[]; 
};
layout(std430, buffer_reference, buffer_reference_align = 8) readonly buffer VertexBuffer {
    Vertex vertices[]; 
};
layout(scalar, buffer_reference, buffer_reference_align = 8) readonly buffer IndexBuffer {
    uint32_t indices[]; 
};

layout(scalar, push_constant) uniform Constants {
    PerTriangleMaterialIds triangle_materials;   
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main()
{
  hitValue = vec3(gl_RayTmaxEXT / 5.0);
  return;
  const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
  const Vertex verts[3] = Vertex[](
    vertex_buffer.vertices[gl_PrimitiveID + 0],
    vertex_buffer.vertices[gl_PrimitiveID + 1],
    vertex_buffer.vertices[gl_PrimitiveID + 2]
  );
  const vec2 uvs = verts[0].uv * attribs.x + verts[1].uv * attribs.y + verts[2].uv * barycentricCoords.x;
  const float t = exp2(5.0 / float(gl_RayTmaxEXT));
  const vec3 color_value = texture(textures[nonuniformEXT(triangle_materials.ids[gl_PrimitiveID])], uvs).rgb * (vec3(uvs.xy, 0.3)*0.5+0.5);
  //const vec3 color_value = vec3(uvs.xy * 0.5 + 0.5, 0.0);
  hitValue = pow(color_value * t, vec3(2.2));
}