#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_ray_tracing : require

layout(location = 0) out vec4 FRAG_COL;
layout(location = 1) out vec4 FRAG_VIEW_POS;
layout(location = 2) out vec4 FRAG_VIEW_NOR;

layout(location = 0) in VertexOutput {
    flat uint mesh_id;
	vec3 pos;
	vec3 nor;
    vec2 uv;
    vec3 tang;
    vec3 bitang;
} vert;

#define RAYTRACING
#define RAYQUERY

#include "../global_common.inc.glsl"
#include "../global_layout.inc.glsl"
#include "push_constants.inc.glsl"
#include "../rtbasic/common.inc.glsl"
#include "../rtbasic/probes.inc.glsl"
#include "../rtbasic/light.inc"

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

		vec3 probe_to_biased_point_dir = biased_world_pos - probe_pos;
#if CHEBYSHEV
		{
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

		weight *= trilinear * max(0.05, dot(probe_to_biased_point_dir, normal));

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

void main() {
	MeshData md = mesh_datas.at[vert.mesh_id];

	vec3 cam_pos = vec3(globals.viewInverse * vec4(0.0, 0.0, 0.0, 1.0));
	vec3 irr = sample_irradiance(vert.pos, vert.nor, cam_pos);
	vec3 col1 = texture(textures[nonuniformEXT(md.color_texture)], vert.uv).rgb;
	vec3 nor = vert.nor;
	float metalness = 0.01;
	float roughness = 1.0;
	
	if(md.normal_texture != ~0) {
		mat3 TBN = mat3(vert.tang, vert.bitang, vert.nor);
		nor = texture(textures[nonuniformEXT(md.normal_texture)], vert.uv).rgb;
		nor = normalize(TBN * normalize(nor * 2.0 - 1.0));
	}
	if(md.metallic_roughness_texture != ~0) {
		vec2 mr = texture(textures[nonuniformEXT(md.metallic_roughness_texture)], vert.uv).gb;
		roughness = mr.x;
		metalness = mr.y;
	}
	//final_color = irr * 2.0 * col1;
	//FRAG_COL = vec4(final_color, 1.0);
	//final_color = calc_direct_lighting_Cook_Torrance_BRDF(vert.pos, nor, cam_pos, col1, metalness, roughness);
	//final_color = vec3(BRDF_Smith_GGX_G2(vert.nor, normalize(lights[0] - vert.pos), normalize(cam_pos - vert.pos), roughness*roughness));
	vec3 final_color = vec3(0.0);
	//final_color = calc_light(vert.pos, cam_pos, vert.nor, col1, metalness, roughness);
	final_color = calc_direct_lighting(vert.pos, vert.nor, cam_pos, col1, irr, 1.0);
	//FRAG_COL = vec4(vec3(final_color), 1.0);
	FRAG_COL = vec4(vec3(final_color), 1.0);
	FRAG_VIEW_POS = vec4(vec3(globals.view * vec4(vert.pos, 1.0)), 0.0);
	FRAG_VIEW_NOR = vec4(vec3(globals.view * vec4(nor, 0.0)), 0.0);
}
