layout(location = 0) 
#ifdef SHADER_TYPE_RAYGEN
rayPayloadEXT
#else
rayPayloadInEXT 
#endif

struct RayPayload {
    vec3 radiance;
    vec3 albedo;
    float distance;
} payload;
