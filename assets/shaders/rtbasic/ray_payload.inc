layout(location = 0) 
#ifdef SHADER_TYPE_RAYGEN
rayPayloadEXT
#else
rayPayloadInEXT 
#endif

struct RayPayload {
    vec3 radiance;
    float distance;
} payload;
