const int num_lights = 1;
const vec3 lights[] = {
	vec3(0.0, 0.5, 0.0),
	vec3(-10, 7.9, -0.09),
	vec3(0.04, 0.24, 0.66),
	vec3(1.0, 8.9, -1.5),
	vec3(-1.0, 5.0, 3.8),
};

float calc_shadow(vec3 o, vec3 d, float max) {
	float shadow = 0.0;
#if defined(RAYQUERY)
	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, o, 0.01, d, max);
	rayQueryProceedEXT(rq);
	if(rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		shadow += 1.0;
	}
#elif defined(RAYTRACING)
    const uint sbtOffset = 3;
    const uint sbtStride = 0;
    const uint missIndex = 1;
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, sbtOffset + 1, sbtStride, missIndex + 1, o, 0.01, d, max, 1);
    shadow += (max > payload_shadow.distance) ? 0.0 : 1.0;
#else 
	shadow = 1.0;
#endif
	return shadow;
}

vec3 calc_direct_lighting(vec3 p, vec3 n, vec3 cam_pos, vec3 albedo, vec3 indirect, float use_shadows) {
	float sum = 0.0;
	for(int i=0; i < num_lights; ++i) {
		vec3 pl = normalize(lights[i] - p);
		float inv_dist2 = 1.0 / distance(p, lights[i]);
		inv_dist2 = pow(inv_dist2, 2.0) * 1.4;
		//sum += max(0.0, dot(pl, n)) * inv_dist2 * 1.0; /* hardcoded strength */
        vec3 V = normalize(cam_pos - p);
        vec3 O = p + 0.01*n + 0.01*V;
        O = p;
        vec3 L = (lights[i] - O);
        float maxL = length(L);
        L /= maxL;
        sum += max(0.0, dot(pl, n)) * inv_dist2 * calc_shadow(p, L, maxL);
	}
    sum /= float(num_lights);
    return (sum + indirect) * albedo;
}
