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

layout(scalar, push_constant) uniform Constants {
    PerTriangleMaterialIds triangle_materials;   
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
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

vec3 normalToUvRectOct(vec3 normal){
    normal /= dot(vec3(1.0), abs(normal));
    
    if(normal.z < 0.0) {
         normal.xy = (1.0 - abs(normal.yx)) * sign(normal.xy);
    }
    
    return normal.xyz;
}


void main() {
    const vec2 pixelCenter = vec2(gl_LaunchIDEXT.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter / vec2(gl_LaunchSizeEXT.xy);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 origin = cam.viewInverse * vec4(0, 0, 0, 1);
    vec4 target = cam.projInverse * vec4(d.x, -d.y, 1, 1);
    vec4 direction = cam.viewInverse * vec4(normalize(target.xyz), 0);

    float tmin = 0.001;
    float tmax = 10000.0;

    hitValue = vec3(0.0);

    const uint cull_mask = 0xFF;
    const uint sbtOffset = 3;
    const uint sbtStride = 1;
    const uint missIndex = 1;
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask, sbtOffset, sbtStride, missIndex, origin.xyz, tmin, direction.xyz, tmax, 0);

    if(mode == 0) {
		imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
	} else if (mode == 1) {
		for(int i=0; i<64; ++i) {
			vec3 dir = sphericalFibonacci(float(i), 64.0);
			vec2 pos = normalToUvRectOct(dir).xy * 0.5 + 0.5;
			pos *= 8;
			imageStore(imageIrradiance, ivec2(pos.xy), vec4(1.0));
		}
    }
}