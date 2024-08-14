#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

#define RAYTRACING
#define SHADER_TYPE_RAYGEN

#include "../global_common.inc.glsl"
#include "common.inc.glsl"
#include "ray_payload.inc"
#include "../global_layout.inc.glsl"
#include "push_constants.inc.glsl"
#include "probes.inc.glsl"
#include "light.inc"

#if 1
const vec4 colors[] = vec4[](
	vec4(0.0, 1.0, 0.0, 1.0),
	vec4(1.0, 0.0, 0.0, 1.0),
	vec4(0.0, 1.0, 0.0, 1.0),
	vec4(1.0, 0.0, 0.0, 1.0),
	vec4(0.0, 1.0, 0.0, 1.0),
	vec4(1.0, 0.0, 0.0, 1.0),
	vec4(0.0, 1.0, 0.0, 1.0),
	vec4(1.0, 0.0, 0.0, 1.0)
);
#endif

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

void main() {
    const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 origin = globals.viewInverse * vec4(0.0, 0.0, 0.0, 1);
    vec4 target = globals.projInverse * vec4(vec3(vec4(d.x, -d.y, 1, 1).xyz), 1.0);
    vec4 direction = globals.viewInverse * vec4(normalize(target.xyz), 0);

	//direction = vec4(globals.randomRotation * direction.xyz, 0.0);

#if 1
	//const float t = float(ddgi.frame_num + 21500) * 0.0001;

	/*direction.xyz = mat3(
		vec3(cos(t), 0.0, -sin(t)),
		vec3(0.0, 1.0, 0.0),
		vec3(sin(t), 0.0, cos(t))
	) * direction.xyz;*/
#endif

    float tmin = 0.00001;
    float tmax = 15.0;

    payload.radiance = vec3(0.0);
    payload.distance = 0.0;

    const uint cull_mask = 0xFE;
    const uint sbtOffset = 3;
    const uint sbtStride = 0;
    const uint missIndex = 1;

    if(mode == MODE_OUTPUT) {
		traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask | 1, sbtOffset, sbtStride, missIndex, origin.xyz, tmin, direction.xyz, tmax, 0);
		imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(payload.radiance, 1.0)); 
		return;
#if 1
		if(payload.distance < 0.0 || payload.distance > tmax) {
			imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(vec3(0.0), 1.0)); 		
			return;
		}

		const vec3 hit_pos = origin.xyz + direction.xyz * payload.distance;
		vec3 final_color = payload.radiance;

#if GLOBAL_ILLUMINATION
		vec3 irradiance = sample_irradiance(hit_pos, payload.normal, origin.xyz);
		final_color += irradiance * payload.albedo;
#endif

		//final_color = mix(calc_direct_lighting(hit_pos, payload.normal) * payload.albedo * payload.shadow, final_color, clamp(sin(t*6.0*PI) * 2.0, 0.0, 1.0));
		final_color = pow(final_color, vec3(1.0 / 2.2));

		imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(final_color, 1.0)); 
#endif
	} else if (mode == MODE_RADIANCE) {
        const ivec2 pixel_coord = ivec2(gl_LaunchIDEXT.xy); 
        const int probe_idx = pixel_coord.y;
        const int ray_idx = pixel_coord.x;

        ivec3 probe_grid_indices = probe_index_to_grid_indices(probe_idx);

		// TODO: make no offset variant
        vec3 ray_origin = grid_indices_to_world(probe_grid_indices, probe_idx);

#if RANDOM_ROTATION
        vec3 ray_dir = normalize(globals.randomRotation * spherical_fibonacci(float(ray_idx), float(ddgi.rays_per_probe)));
#else
        vec3 ray_dir = spherical_fibonacci(float(ray_idx), float(ddgi.rays_per_probe));
#endif

		traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask, sbtOffset, sbtStride, missIndex, ray_origin, ddgi.min_probe_distance, ray_dir, ddgi.max_probe_distance, 0);

        imageStore(ddgi_radiance_image, pixel_coord, vec4(payload.radiance, payload.distance));
    }
}