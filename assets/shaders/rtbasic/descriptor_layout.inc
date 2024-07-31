#ifdef RAYTRACING
layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
#endif
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0, rgba16f) uniform image2D ddgi_radiance_image;
layout(binding = 3, set = 0, rgba16f) uniform image2D ddgi_irradiance_image;
layout(binding = 4, set = 0, rg16f) uniform image2D ddgi_visibility_image;
layout(binding = 5, set = 0, rgba16f) uniform image2D ddgi_probe_offset_image;
layout(binding = 14, set = 0) uniform CameraProperties {
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation;
} cam;
layout(binding = 15, set = 0) uniform sampler2D textures[];
