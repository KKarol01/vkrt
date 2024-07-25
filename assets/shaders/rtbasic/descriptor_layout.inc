layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0, rgba8) uniform image2D ddgi_radiance_image;
layout(binding = 3, set = 0, rgba8) uniform image2D ddgi_irradiance_image;
layout(binding = 4, set = 0) uniform sampler ddgi_irradiance_sampler;
layout(binding = 14, set = 0) uniform CameraProperties {
    mat4 viewInverse;
    mat4 projInverse;
    mat3 randomRotation;
} cam;
layout(binding = 15, set = 0) uniform sampler2D textures[];
