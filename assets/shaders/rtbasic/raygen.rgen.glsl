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

void main() {
    const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 origin = globals.viewInverse * vec4(0.0, 0.0, 0.0, 1);
    vec4 target = globals.projInverse * vec4(vec3(vec4(d.x, -d.y, 1, 1).xyz), 1.0);
    vec4 direction = globals.viewInverse * vec4(normalize(target.xyz), 0);

	direction = vec4(globals.randomRotation * direction.xyz, 0.0);

    float tmin = 0.00001;
    float tmax = 15.0;

    payload.radiance = vec3(0.0);
    payload.distance = 0.0;

    const uint cull_mask = 0xFE;
    const uint sbtOffset = 3;
    const uint sbtStride = 0;
    const uint missIndex = 1;

    if(mode == 0) {
#if 0
		traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask | 1, sbtOffset, sbtStride, missIndex, origin.xyz, tmin, direction.xyz, tmax, 0);

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

		imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(final_color, 1.0)); 
#endif
	} else if (mode == 1) {
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