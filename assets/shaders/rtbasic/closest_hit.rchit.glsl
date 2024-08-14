#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#define RAYTRACING

#include "common.inc.glsl"
#include "../global_common.inc.glsl"
#include "ray_payload.inc"
#include "push_constants.inc.glsl"
#include "../global_layout.inc.glsl"
#include "light.inc"
#include "probes.inc.glsl"

layout(location = 1) rayPayloadEXT struct RayPayloadShadow {
    float distance;
} payload_shadow;

hitAttributeEXT vec2 barycentric_weights;

vec3 sample_irradiance(vec3 world_pos, vec3 normal, vec3 cam_pos) {
	vec3 V = normalize(cam_pos - world_pos);
	vec3 bias_vec = (normal * 0.2 + V * 0.8) * (0.75 * 1.0) * ddgi.normal_bias;
	vec3 biased_world_pos = world_pos + bias_vec;
	ivec3 base_grid_indices = world_to_grid_indices(biased_world_pos);
	vec3 base_probe_world_pos = grid_indices_to_world_no_offsets(base_grid_indices);
	vec3 alpha = clamp((biased_world_pos - base_probe_world_pos) / ddgi.probe_walk, vec3(0.0), vec3(1.0));

	vec3 irr = vec3(0.0);
	float sum_weight = 0.0;

#if 1
	for(int i=0; i<8; ++i) {
		ivec3 offset = ivec3(i, i>>1, i>>2) & ivec3(1);

		vec3 trilinear3 = mix(1.0 - alpha, alpha, vec3(offset));
		float trilinear = trilinear3.x * trilinear3.y * trilinear3.z + 0.0001;

		ivec3 probe_grid_coord = clamp(base_grid_indices + offset, ivec3(0), ivec3(ddgi.probe_counts) - ivec3(1));
		int probe_idx = probe_indices_to_index(probe_grid_coord);

		vec3 probe_pos = grid_indices_to_world(probe_grid_coord, probe_idx);
		
		float weight = 1.0;

		// TODO: Use smooth backfaces (?)

#if CHEBYSHEV
		{
			vec3 probe_to_biased_point_dir = biased_world_pos - probe_pos;
			float dist_to_biased_point = length(probe_to_biased_point_dir);
			probe_to_biased_point_dir *= 1.0 / dist_to_biased_point;

			vec2 uv = get_probe_uv(probe_to_biased_point_dir, probe_idx, ddgi.visibility_tex_size.x, ddgi.visibility_tex_size.y, int(ddgi.vis_res));
			vec2 visibility = textureLod(ddgi_visibility_texture, uv, 0).rg;
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
		}
#endif
		const float crush_threshold = 0.2;
		if(weight < crush_threshold) {
			weight *= (weight * weight) * (1.0 / (crush_threshold * crush_threshold));
		}

		weight *= trilinear;

#if 0
		vec3 probe_irr = colors[probe_idx].xyz;
#else
		vec3 probe_irr = textureLod(ddgi_irradiance_texture, get_probe_uv(normal, probe_idx, ddgi.irradiance_tex_size.x, ddgi.irradiance_tex_size.y, ddgi.irr_res), 0).rgb;
		//probe_irr = vec3(get_probe_uv(normal, probe_coord), 0.0);
#endif
		irr += weight * probe_irr;
		sum_weight += weight;
	}
#endif

	return (irr / sum_weight) * 0.5 * PI;
}

float calc_shadow(vec3 o, vec3 n) {
    float tmin = 0.00001;
    float tmax = 5.0;

    const uint cull_mask = 0xFE;
    const uint sbtOffset = 3;
    const uint sbtStride = 0;
    const uint missIndex = 1;

	float shadow = 0.0;
	for(int i=0; i<num_lights && shadow < EPSILON; ++i) {
		vec3 pl = lights[i] - o;
		float pld = length(pl);
		pl /= pld;
		traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask | 1, sbtOffset + 1, sbtStride, missIndex + 1, o + n * 0.01 - pl * 0.01, tmin, pl, pld, 1);
		shadow += (pld > payload_shadow.distance) ? 0.0 : 1.0;
	}
	return shadow;
}

void main()
{
  if(gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT) {
	payload.albedo = vec3(0.0);
	payload.normal = vec3(0.0);
    payload.radiance = vec3(0.0);
    payload.distance = gl_RayTmaxEXT * -0.2;
	payload.shadow = 0.0;
    return;
  }

  const uint32_t triangle_offset = blas_mesh_offsets.at[tlas_mesh_offsets.at[gl_InstanceID] + gl_GeometryIndexEXT];
  const uint32_t mesh_id = triangle_mesh_ids.at[triangle_offset + gl_PrimitiveID];
  const MeshData mesh_data = mesh_datas.at[mesh_id];

  const uint32_t i0 = index_buffer.at[mesh_data.index_offset + gl_PrimitiveID * 3 + 0] + mesh_data.vertex_offset;
  const uint32_t i1 = index_buffer.at[mesh_data.index_offset + gl_PrimitiveID * 3 + 1] + mesh_data.vertex_offset;
  const uint32_t i2 = index_buffer.at[mesh_data.index_offset + gl_PrimitiveID * 3 + 2] + mesh_data.vertex_offset;
  const Vertex v0 = vertex_buffer.at[i0];
  const Vertex v1 = vertex_buffer.at[i1];
  const Vertex v2 = vertex_buffer.at[i2];

  const float b = barycentric_weights.x;
  const float c = barycentric_weights.y;
  const float a = 1.0 - b - c;

  const vec3 pos = v0.pos * a + v1.pos * b + v2.pos * c;
  const vec2 uvs = v0.uv * a + v1.uv * b + v2.uv * c;
  const vec3 nor = v0.nor * a + v1.nor * b + v2.nor * c;

  const vec3 color_value = texture(textures[nonuniformEXT(mesh_data.color_texture)], uvs).rgb;
  const float shadow = calc_shadow(pos, nor);
  payload.radiance = color_value * calc_direct_lighting(pos, nor) * shadow;

#if GLOBAL_ILLUMINATION && TEMPORAL_ACCUMULATION
	payload.radiance += color_value * sample_irradiance(pos, nor, gl_ObjectRayOriginEXT.xyz) * 0.75;
#endif

  payload.albedo = color_value;
  payload.normal = nor;
  payload.distance = gl_RayTminEXT + gl_HitTEXT;
  payload.shadow = shadow;
}

/*
    gl_InstanceID - instance of BLAS in TLAS
    gl_GeometryIndexEXT - idx of geometry inside one BLAS (currently it's always 0)
    gl_PrimitiveID - local for each geometry inside BLAS
*/