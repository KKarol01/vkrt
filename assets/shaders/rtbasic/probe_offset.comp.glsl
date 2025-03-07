#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

#include "../global_common.inc.glsl"
#include "common.inc.glsl"
#include "../global_layout.inc.glsl"
#include "push_constants.inc.glsl"
#include "probes.inc.glsl"

void main() {
	const int probe_idx = int(gl_GlobalInvocationID.x);
	const int total_probes = int(ddgi.probe_counts.x * ddgi.probe_counts.y * ddgi.probe_counts.z);

	if(probe_idx >= total_probes) { return; }

	int closest_backface_index = -1;
	int closest_frontface_index = -1;
	int farthest_frontface_index = -1;
	float closest_frontface_distance = 100000.0;
	float closest_backface_distance = 100000.0;
	float farthest_frontface_distance = 0.0;
	int backface_count = 0;
	const int probes_xy = int(ddgi.probe_counts.x * ddgi.probe_counts.y);
	const ivec2 offset_coords = ivec2(probe_idx % probes_xy, probe_idx / probes_xy);

	for(int ray_idx = 0; ray_idx < ddgi.rays_per_probe; ++ray_idx) {
		ivec2 ray_coord = ivec2(ray_idx, probe_idx);
		float dist = imageLoad(ddgi_radiance_image, ray_coord).w;

		if(dist <= 0.0) {
			++backface_count;
			if(-dist < closest_backface_distance) {
				closest_backface_distance = -dist;
				closest_backface_index = ray_idx;
			}
		} else {
			if(dist < closest_frontface_distance) {
				closest_backface_distance = dist;
				closest_frontface_index = ray_idx;
			} else if(dist > farthest_frontface_distance) {
				farthest_frontface_distance = dist;
				farthest_frontface_index = ray_idx;
			}
		}
	}

	vec4 current_offset = vec4(0.0);
	//if(ddgi.frame_num > 0) { // TODO: Don't think this is neccessary (the if itself)
		current_offset.xyz = imageLoad(ddgi_probe_offset_image, offset_coords).xyz;
	//}

	vec3 full_offset = vec3(10000.0);
	vec3 offset_limit = ddgi.max_probe_offset * ddgi.probe_walk;
	const bool is_inside_geometry = (float(backface_count) / float(ddgi.rays_per_probe)) > 0.25;

	if(is_inside_geometry && closest_backface_index != -1) {
#if RANDOM_ROTATION
		const vec3 closest_backface_dir = closest_backface_distance * normalize(globals.randomRotation * spherical_fibonacci(float(closest_backface_index), float(ddgi.rays_per_probe)));
#else
		const vec3 closest_backface_dir = closest_backface_distance * spherical_fibonacci(float(closest_backface_index), float(ddgi.rays_per_probe));
#endif

		const vec3 pos_offset = (current_offset.xyz + offset_limit) / closest_backface_dir;
		const vec3 neg_offset = (current_offset.xyz - offset_limit) / closest_backface_dir;
		const vec3 max_offset = vec3(
			max(pos_offset.x, neg_offset.x),
			max(pos_offset.y, neg_offset.y),
			max(pos_offset.z, neg_offset.z));
		const float dir_scale = min(min(max_offset.x, max_offset.y), max_offset.z) - 0.001;
		
		full_offset = current_offset.xyz + closest_backface_dir * dir_scale;
	} else if(closest_frontface_distance < 0.05) {
#if RANDOM_ROTATION
		const vec3 farthest_dir = min(0.2, farthest_frontface_distance) * normalize(globals.randomRotation * spherical_fibonacci(float(farthest_frontface_index), float(ddgi.rays_per_probe)));
		const vec3 closest_dir = normalize(globals.randomRotation * spherical_fibonacci(float(closest_frontface_index), float(ddgi.rays_per_probe)));
#else
		const vec3 farthest_dir = min(0.2, farthest_frontface_distance) * spherical_fibonacci(float(farthest_frontface_index), float(ddgi.rays_per_probe));
		const vec3 closest_dir = spherical_fibonacci(float(closest_frontface_index), float(ddgi.rays_per_probe));
#endif

		if(dot(normalize(farthest_dir), closest_dir) < 0.5) {
			full_offset = current_offset.xyz + farthest_dir;
		}
	}

	if(all(lessThan(abs(full_offset), offset_limit))) {
		current_offset.xyz = full_offset;
	}

	imageStore(ddgi_probe_offset_image, offset_coords, current_offset);
	ddgi.debug_probe_offsets.at[probe_idx].xyz = current_offset.xyz;
}