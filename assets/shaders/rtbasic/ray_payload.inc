layout(location = 0) 
#ifdef SHADER_TYPE_RAYGEN
rayPayloadEXT
#else
rayPayloadInEXT 
#endif

struct RayPayload {
    vec3 radiance;
    float distance;
    vec3 albedo;
    float shadow;
    vec3 normal;
} payload;