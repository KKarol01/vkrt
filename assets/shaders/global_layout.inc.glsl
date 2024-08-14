#ifdef RAYTRACING
layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
#endif
layout(binding = 1, set = 0, rgba8) uniform image2D image;
layout(binding = 2, set = 0, rgba16f) uniform image2D ddgi_radiance_image;
layout(binding = 3, set = 0) uniform sampler2D ddgi_irradiance_texture;
layout(binding = 4, set = 0) uniform sampler2D ddgi_visibility_texture;
layout(binding = 5, set = 0, rgba16f) uniform image2D ddgi_probe_offset_image;
layout(binding = 6, set = 0, rgba16f) uniform image2D ddgi_irradiance_image;
layout(binding = 7, set = 0, rg16f) uniform image2D ddgi_visibility_image;
layout(binding = 15, set = 0) uniform sampler2D textures[];
