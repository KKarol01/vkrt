#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "../global_common.inc.glsl"
#include "common.inc.glsl"
#include "../global_layout.inc.glsl"
#include "push_constants.inc.glsl"
#include "probes.inc.glsl"

// TODO: the tables should be generated if any probe resolution (side length) should change
const int k_read_table[] = int[](
    5, 3, 1, -1, -3, -5
);
#if 1
const int k_read_table_vis[] = int[](
    13, 11, 9, 7, 5, 3, 1, -1, -3, -5, -7, -9, -11, -13
);
#endif

const vec3 dirs[] = {
	vec3(-1.0, 0.0, 0.0),
	vec3( 1.0, 0.0, 0.0),
	vec3(-1.0, 0.0, 0.0),
	vec3( 1.0, 0.0, 0.0),
	vec3(-1.0, 0.0, 0.0),
	vec3( 1.0, 0.0, 0.0),
	vec3(-1.0, 0.0, 0.0),
	vec3( 1.0, 0.0, 0.0)
};

void main() {
#if 0
	imageStore(ddgi_irradiance_image, ivec2(gl_GlobalInvocationID.xy), vec4(0.0));
	imageStore(ddgi_visibility_image, ivec2(gl_GlobalInvocationID.xy), vec4(0.0));
	//return;
#endif
	const ivec2 coords = ivec2(gl_GlobalInvocationID.xy);
	const int probe_texture_width = (mode == MODE_IRRADIANCE) ? ddgi.irradiance_tex_size.x : ddgi.visibility_tex_size.x;
	const int probe_texture_height = (mode == MODE_IRRADIANCE) ? ddgi.irradiance_tex_size.y : ddgi.visibility_tex_size.y;
	const int probe_res = (mode == MODE_IRRADIANCE) ? ddgi.irr_res : ddgi.vis_res;
	const int probe_with_border = probe_res + 2;
	const int probe_last_pixel = probe_res + 1;
	const int probe_id = get_probe_index_from_pixels(coords, probe_with_border, probe_texture_width);

	if(coords.x >= probe_texture_width || coords.y >= probe_texture_height) { return; }

	bool is_border_pixel = (
		   (gl_GlobalInvocationID.x % probe_with_border) == 0
		|| (gl_GlobalInvocationID.x % probe_with_border) == probe_last_pixel
		|| (gl_GlobalInvocationID.y % probe_with_border) == 0
		|| (gl_GlobalInvocationID.y % probe_with_border) == probe_last_pixel
	);

	if(!is_border_pixel) {
		vec4 result = vec4(0.0);
		const float energy_conservation = 0.95;
		uint num_backfaces = 0;
		uint max_backfaces = uint(ddgi.rays_per_probe * 0.1);

		for(int ray_idx = 0; ray_idx < ddgi.rays_per_probe; ++ray_idx) {
			ivec2 sample_pos = ivec2(ray_idx, probe_id);
#if RANDOM_ROTATION
			vec3 ray_dir = normalize(globals.randomRotation * spherical_fibonacci(float(ray_idx), float(ddgi.rays_per_probe)));
#else
			vec3 ray_dir = spherical_fibonacci(float(ray_idx), float(ddgi.rays_per_probe));
#endif
			vec3 texel_dir = oct_decode(normalized_oct_coord(coords, probe_res));
			float weight = max(0.0, dot(texel_dir, ray_dir));
			vec4 read_radiance_distance = imageLoad(ddgi_radiance_image, sample_pos);
			vec3 radiance = read_radiance_distance.rgb;
			float dist = read_radiance_distance.w;

			if(dist < 0.0) {
				++num_backfaces;
				if(num_backfaces >= max_backfaces) { return; }
				continue;
			}

			if(mode == MODE_IRRADIANCE) {
				if(weight > EPSILON) {
					radiance *= energy_conservation;
					result += vec4(radiance * weight, weight); 
				}
			} else if (mode == MODE_VISIBILITY) {
				weight = pow(weight, 2.5);

				if(weight > EPSILON) {
					dist = min(abs(dist), ddgi.max_probe_distance /* * 1.5 */);
					result += vec4(dist * weight, (dist * dist) * weight, 0.0, weight);
				}
			}
		}

		if(result.w > EPSILON) { result.xyzw /= result.w; }

		if(mode == MODE_IRRADIANCE) {
#ifdef HYSTERESIS
			vec4 prev_result = imageLoad(ddgi_irradiance_image, coords);
			vec3 diff = abs(result.rgb - prev_result.rgb);
			float change_magnitude = max(max(diff.x, diff.y), diff.z);
			float h = HYSTERESIS;
			result.rgb = mix(result.rgb, prev_result.rgb, HYSTERESIS);
#endif
			imageStore(ddgi_irradiance_image, coords, vec4(result.xyz, 1.0));
		} else if(mode == MODE_VISIBILITY) {
#ifdef HYSTERESIS
			vec2 prev_result = imageLoad(ddgi_visibility_image, coords).rg;
			result.rg = mix(result.rg, prev_result, HYSTERESIS);
#endif
			imageStore(ddgi_visibility_image, coords, vec4(result.rg, 0.0, 1.0));
		}
		return;
	} else { 
#if 0
		imageStore(ddgi_irradiance_image, coords, vec4(1.0)); 
		return; 
#endif
	}
     
    groupMemoryBarrier();
    barrier();

    const int probe_pixel_x = coords.x % probe_with_border;
    const int probe_pixel_y = coords.y % probe_with_border;
    const bool is_corner_pixel = (
           (probe_pixel_x == 0 || probe_pixel_x == probe_last_pixel)
        && (probe_pixel_y == 0 || probe_pixel_y == probe_last_pixel)
    );
    const bool is_row_pixel = (probe_pixel_x > 0 && probe_pixel_x < probe_last_pixel);
    ivec2 source_pixel_coord = coords;

    if(is_corner_pixel) {
        source_pixel_coord.x += probe_pixel_x == 0 ? probe_res : -probe_res;
        source_pixel_coord.y += probe_pixel_y == 0 ? probe_res : -probe_res;
    } else if(is_row_pixel) {
		if(mode == MODE_IRRADIANCE) source_pixel_coord.x += k_read_table[probe_pixel_x - 1];
		else if(mode == MODE_VISIBILITY) source_pixel_coord.x += k_read_table_vis[probe_pixel_x - 1];
        source_pixel_coord.y += (probe_pixel_y > 0) ? -1 : 1;
    } else {
        source_pixel_coord.x += (probe_pixel_x > 0) ? -1 : 1;
        if(mode == MODE_IRRADIANCE) source_pixel_coord.y += k_read_table[probe_pixel_y - 1];
        else if(mode == MODE_VISIBILITY) source_pixel_coord.y += k_read_table_vis[probe_pixel_y - 1];
    }

	if(mode == MODE_IRRADIANCE) {
		vec4 copied_data = imageLoad(ddgi_irradiance_image, source_pixel_coord);
		imageStore(ddgi_irradiance_image, coords, copied_data);
	} else if(mode == MODE_VISIBILITY) {
		vec4 copied_data = imageLoad(ddgi_visibility_image, source_pixel_coord);
		imageStore(ddgi_visibility_image, coords, copied_data);
	}
}