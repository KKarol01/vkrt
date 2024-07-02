#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec2 attribs;

void main()
{
  const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
  const vec3 colors[] = vec3[](
    vec3(0.3, 0.5, 0.1),
    vec3(0.4, 0.1, 0.8),
    vec3(0.8, 0.9, 0.3)
  );

  const float t = float(gl_RayTmaxEXT) / 5.0;
  hitValue = mix(colors[gl_InstanceID], colors[gl_InstanceID + 1], t);
}