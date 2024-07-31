#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#include "ray_payload.inc"
#include "push_constants.inc"
#include "descriptor_layout.inc"
#include "light.inc"
#include "common.inc"
#include "probes.inc.glsl"

hitAttributeEXT vec2 barycentric_weights;

vec3 sample_irradiance(vec3 world_pos, vec3 normal, vec3 cam_pos) {
	vec3 V = normalize(cam_pos - world_pos);
	vec3 bias_vec = (normal * 0.2 + V * 0.8) * (0.75 * float(ddgi.min_dist)) * ddgi.normal_bias;
	vec3 biased_world_pos = world_pos + bias_vec;
	ivec3 grid_indices = world_to_grid_coords(biased_world_pos);
	vec3 grid_pos = grid_coord_to_position(grid_indices);
	vec3 alpha = clamp((biased_world_pos - grid_pos) / ddgi.probe_walk, vec3(0.0), vec3(1.0));

	vec3 irr = vec3(0.0);
	float sum_weight = 0.0;

	//return irr;

#if 1
	for(int i=0; i<8; ++i) {
		ivec3 offset = ivec3(i, i>>1, i>>2) & ivec3(1);

		vec3 trilinear3 = max(vec3(0.001), mix(1.0 - alpha, alpha, vec3(offset)));
		float trilinear = trilinear3.x * trilinear3.y * trilinear3.z + 0.0001;

		ivec3 probe_coord = clamp(grid_indices + offset, ivec3(0), ivec3(ddgi.probe_counts) - 1);
		int probe_idx = get_probe_index_from_grid_coords(probe_coord);

		// TODO: make no offset variant
		vec3 probe_pos = grid_coord_to_position_offset(probe_coord, probe_idx);
		
		float weight = 1.0;

#if CHEBYSHEV
		{
			vec3 probe_to_biased_point_dir = biased_world_pos - probe_pos;
			float dist_to_biased_point = length(probe_to_biased_point_dir);
			probe_to_biased_point_dir *= 1.0 / dist_to_biased_point;

			vec2 uv = get_probe_uv(probe_to_biased_point_dir, probe_coord, ddgi.vis_res);
			vec2 visibility = textureLod(textures[ddgi.radiance_tex_idx + 2], uv, 0).rg;
			float mean_dist = visibility.x;
			float chebyshev = 1.0;

			if(dist_to_biased_point > mean_dist) {
				float variance = abs((visibility.x * visibility.x) - visibility.y);
				const float distance_diff = dist_to_biased_point - mean_dist;
				chebyshev = variance / (variance + (distance_diff * distance_diff));
				chebyshev = max((chebyshev * chebyshev * chebyshev), 0.0);
			}

			chebyshev = max(0.05, chebyshev);
			weight *= chebyshev;
			weight = max(0.00001, weight);
		
			const float crush_threshold = 0.2;
			if(weight < crush_threshold) {
				weight *= (weight * weight) * (1.0 / (crush_threshold * crush_threshold));
			}
		}
#endif
		
		weight *= trilinear;

#if 0
		vec3 probe_irr = colors[probe_idx].xyz;
#else
		vec3 probe_irr = textureLod(textures[ddgi.radiance_tex_idx + 1], get_probe_uv(normal, probe_coord, ddgi.irr_res), 0).rgb;
		//probe_irr = vec3(get_probe_uv(normal, probe_coord), 0.0);
#endif
		irr += weight * probe_irr;
		sum_weight += weight;
	}
#endif

	return (irr / sum_weight) * 0.5 * PI;
}

void main()
{
  if(gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT) {
    payload.radiance = vec3(0.0);
    payload.distance = gl_RayTmaxEXT * -0.2;
    return;
  }

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

#if GLOBAL_ILLUMINATION && TEMPORAL_ACCUMULATION
	 payload.radiance += color_value * sample_irradiance(pos, nor, gl_ObjectRayOriginEXT.xyz) * 0.75;
#endif
  payload.albedo = color_value;
  payload.distance = gl_RayTminEXT + gl_HitTEXT;
}

/*
    gl_InstanceID - instance of BLAS in TLAS
    gl_GeometryIndexEXT - idx of geometry inside one BLAS (currently it's always 0)
    gl_PrimitiveID - local for each geometry inside BLAS
*/