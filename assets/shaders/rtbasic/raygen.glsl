#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0, rgba16f) uniform image2D imageIrradiance;
layout(binding = 3, set = 0) uniform CameraProperties {
    mat4 viewInverse;
    mat4 projInverse;
} cam;
layout(binding = 4, set = 0) uniform sampler2D textures[];

layout(location = 0) rayPayloadEXT vec3 hitValue;

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
    // const uint missIndex = (d.x >= 0.8 ? 1 : 0) + 1;
    const uint missIndex = 1;
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, cull_mask, sbtOffset, sbtStride, missIndex, origin.xyz, tmin, direction.xyz, tmax, 0);

    imageStore(image, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
    imageStore(imageIrradiance, ivec2(10, 10), vec4(hitValue, 1.0));
}