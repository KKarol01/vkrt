#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable

const float PI = 3.1415926535897932;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0, rgba16f) uniform image2D imageIrradiance;
layout(binding = 3, set = 0) uniform CameraProperties {
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation;
} cam;
layout(binding = 4, set = 0) uniform sampler2D textures[];

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
layout(scalar, buffer_reference, buffer_reference_align = 4) readonly buffer DDGIBuffer {
	vec3 probe_start;
	uvec3 probe_counts;
	vec3 probe_walk;
	float min_dist;
	float max_dist;
	float normal_bias;
	uint irr_res;
    uint rays_per_probe;
    uint irr_tex_idx;
};

layout(scalar, push_constant) uniform Constants {
    PerTriangleMaterialIds triangle_materials;   
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    DDGIBuffer ddgi;
    uint32_t mode;
};

layout(location = 0) rayPayloadEXT vec3 hitValue;


vec3 sphericalFibonacci(float i, float n) {
    const float PHI = sqrt(5.0) * 0.5 + 0.5;
#   define madfrac(A, B) ((A)*(B)-floor((A)*(B)))
    float phi = 2.0 * PI * madfrac(i, PHI - 1);
    float cosTheta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));

    return vec3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta);

#   undef madfrac
}

vec2 normalToUvRectOct(vec3 normal){
    vec2 p = normal.xy / dot(vec3(1.0), abs(normal));
    
    if(normal.z < 0.0) {
         p.xy = (1.0 - abs(p.yx)) * vec2(normal.x <= 0.0 ? -1 : 1, normal.y <= 0.0 ? -1 : 1);
    }
    
    return clamp(p.xy, vec2(-1.0), vec2(1.0));
}

vec3 grid_coord_to_position(ivec3 grid_coords) {
    return ddgi.probe_start + vec3(grid_coords) * ddgi.probe_walk;
}

ivec3 probe_index_to_grid_coord(int probe_id) {
    ivec3 pos;
    pos.x = probe_id & (int(ddgi.probe_counts.x) - 1);
    pos.y = (probe_id & (int(ddgi.probe_counts.x) * int(ddgi.probe_counts.y) - 1)) >> findMSB(ddgi.probe_counts.x);
    pos.z = probe_id >> findMSB(ddgi.probe_counts.x * ddgi.probe_counts.y);
    return pos;
}

vec3 get_probe_location(int probe_id) {
    return grid_coord_to_position(probe_index_to_grid_coord(probe_id));
}

uint get_probe_id(uvec3 launchid) {
    return launchid.x + launchid.y * ddgi.probe_counts.x + launchid.z * ddgi.probe_counts.x * ddgi.probe_counts.y; 
}

void main() {
    const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 origin = cam.viewInverse * vec4(0, 0, 0, 1);
    vec4 target = cam.projInverse * vec4(d.x, -d.y, 1, 1);
    vec4 direction = cam.viewInverse * vec4(normalize(target.xyz), 0);

    float tmin = 0.1;
    float tmax = 5.0;

    hitValue = vec3(0.0);

    const uint cull_mask = 0xFF;
    const uint sbtOffset = 3;
    const uint sbtStride = 1;
    const uint missIndex = 1;

    if(mode == 0) {
		traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask, sbtOffset, sbtStride, missIndex, origin.xyz, tmin, direction.xyz, tmax, 0);
		imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
	} else if (mode == 1) {
        const uint probe_id = get_probe_id(gl_LaunchIDEXT.xyz);
        const vec3 ray_origin = get_probe_location(int(probe_id));
        const uint irs = ddgi.irr_res + 2;

        for(uint i=0; i<ddgi.rays_per_probe; ++i) {
			vec4 ray_dir = vec4(sphericalFibonacci(float(i), float(ddgi.irr_res)), float(1e10));
            ray_dir.xyz = normalize(cam.randomRotation * ray_dir.xyz);
			//traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask, sbtOffset, sbtStride, missIndex, ray_origin.xyz, ddgi.min_dist, ray_dir.xyz, ddgi.max_dist, 0);

			vec2 pos = (normalToUvRectOct(ray_dir.xyz).xy * 0.5 + 0.5) * ddgi.irr_res;

			ivec3 grid_coord = probe_index_to_grid_coord(int(probe_id));
            vec2 grid_pos_offset = vec2(grid_coord.x * irs + ddgi.probe_counts.x*grid_coord.y*irs, grid_coord.z*ddgi.probe_counts.x*ddgi.probe_counts.y);
			//imageStore(imageIrradiance, ivec2(grid_pos_offset + vec2(1.0, 1.0) + pos), vec4(1.0));
			imageStore(imageIrradiance, ivec2(pos), vec4(1.0));
        }

    }
}