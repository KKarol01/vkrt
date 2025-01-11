const int num_lights = 1;
const vec3 lights[] = vec3[](
    vec3( 0.000,    0.5,  0.000),
    vec3( 0.000,    5.000,  0.000),
    vec3(  10.000,  5.000,  0.000),
    vec3( -10.000,  0.000,  0.000),
    vec3(-5.000,  0.000,  0.000),
    vec3( 0.000,  0.000,  5.000),
    vec3( 0.000,  0.000, -5.000),
    vec3( 3.536,  3.536,  0.000),
    vec3( 3.536, -3.536,  0.000),
    vec3(-3.536,  3.536,  0.000),
    vec3(-3.536, -3.536,  0.000),
    vec3( 3.536,  0.000,  3.536),
    vec3( 3.536,  0.000, -3.536),
    vec3(-3.536,  0.000,  3.536),
    vec3(-3.536,  0.000, -3.536),
    vec3( 0.000,  3.536,  3.536),
    vec3( 0.000, -3.536,  3.536)
);


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

vec3 BRDF_lambertian(vec3 diffuseColor)
{
    // see https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
    return (diffuseColor / PI);
}

float V_GGX(float NdotL, float NdotV, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;

    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);

    float GGX = GGXV + GGXL;
    if (GGX > 0.0)
    {
        return 0.5 / GGX;
    }
    return 0.0;
}

float D_GGX(float NdotH, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float f = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (PI * f * f);
}


vec3 BRDF_specularGGX(float alphaRoughness, float NdotL, float NdotV, float NdotH)
{
    float Vis = V_GGX(NdotL, NdotV, alphaRoughness);
    float D = D_GGX(NdotH, alphaRoughness);

    return vec3(Vis * D);
}

float V_GGX_anisotropic(float NdotL, float NdotV, float BdotV, float TdotV, float TdotL, float BdotL, float at, float ab)
{
    float GGXV = NdotL * length(vec3(at * TdotV, ab * BdotV, NdotV));
    float GGXL = NdotV * length(vec3(at * TdotL, ab * BdotL, NdotL));
    float v = 0.5 / (GGXV + GGXL);
    return clamp(v, 0.0, 1.0);
}

float clampedDot(vec3 a, vec3 b) {
    return clamp(dot(a, b), 0.0, 1.0);
}

vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH) 
{
    return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

vec3 calc_light(vec3 p, vec3 c, vec3 n, vec3 albedo, float metalness, float roughness) {
    vec3 l_color = vec3(0.0);
    for(int i = 0; i < num_lights; ++i) {
        vec3 light = lights[i];
        vec3 pointToLight = light - p;
        vec3 v = normalize(c - p);
        vec3 l = normalize(pointToLight);   // Direction from surface point to light
        vec3 h = normalize(l + v);          // Direction of the vector between l and v, called halfway vector
        float NdotL = clampedDot(n, l);
        float NdotV = clampedDot(n, v);
        float NdotH = clampedDot(n, h);
        float LdotH = clampedDot(l, h);
        float VdotH = clampedDot(v, h);

        vec3 f0 = vec3(0.04);
        float specularWeight = 1.0;
        vec3 f90 = vec3(1.0);
        vec3 dielectric_fresnel = F_Schlick(f0*specularWeight, f90, abs(VdotH));
        vec3 metal_fresnel = F_Schlick(albedo, vec3(1.0), abs(VdotH));

        float lightIntensity = 1.0 * 1.0 / distance(light, p);
        vec3 l_diffuse = lightIntensity * NdotL * BRDF_lambertian(albedo);
        vec3 l_specular_metal = lightIntensity * NdotL * BRDF_specularGGX(roughness*roughness, NdotL, NdotV, NdotH);
        vec3 l_specular_dielectric = l_specular_metal;
        vec3 l_metal_brdf = metal_fresnel * l_specular_metal;
        vec3 l_dielectric_brdf = mix(l_diffuse, l_specular_dielectric, dielectric_fresnel);

        l_color += mix(l_dielectric_brdf, l_metal_brdf, metalness);
    }
    return l_color;
}

vec3 calc_direct_lighting1(vec3 p, vec3 c, vec3 n, vec3 albedo, float metalness, float roughness) { 
    vec3 l_color = vec3(0.0);
    for(int i = 0; i < num_lights; ++i) {
        vec3 light = lights[i];
        vec3 pointToLight = light - p;
        vec3 v = normalize(c - p);
        vec3 l = normalize(pointToLight);   // Direction from surface point to light
        vec3 h = normalize(l + v);          // Direction of the vector between l and v, called halfway vector
        float NdotL = clampedDot(n, l);
        float NdotV = clampedDot(n, v);
        float NdotH = clampedDot(n, h);
        float LdotH = clampedDot(l, h);
        float VdotH = clampedDot(v, h);

        vec3 f0 = vec3(0.04);
        float specularWeight = 1.0;
        vec3 f90 = vec3(1.0);
        vec3 dielectric_fresnel = F_Schlick(f0*specularWeight, f90, abs(VdotH));
        vec3 metal_fresnel = F_Schlick(albedo, vec3(1.0), abs(VdotH));

        float lightIntensity = 16.0 * 1.0 / distance(light, p);
        vec3 l_diffuse = lightIntensity * NdotL * BRDF_lambertian(albedo);
        vec3 l_specular_metal = lightIntensity * NdotL * BRDF_specularGGX(roughness*roughness, NdotL, NdotV, NdotH);
        vec3 l_specular_dielectric = l_specular_metal;
        vec3 l_metal_brdf = metal_fresnel * l_specular_metal;
        vec3 l_dielectric_brdf = mix(l_diffuse, l_specular_dielectric, dielectric_fresnel);

        l_color += l_diffuse;
    }
    return l_color;
}

vec3 calc_direct_lighting(vec3 p, vec3 n, vec3 cam_pos, vec3 albedo, vec3 indirect, float use_shadows) {
    float sum = 0.0;
    for(int i=0; i < num_lights; ++i) {
        vec3 pl = normalize(lights[i] - p);
        float inv_dist2 = 1.0 / max(1.0, distance(p, lights[i]));
        inv_dist2 = pow(inv_dist2, 2.0);
        vec3 V = normalize(cam_pos - p);
        vec3 O = p + 0.01*n + 0.01*V;
        O = p;
        vec3 L = (lights[i] - O);
        float maxL = length(L);
        L /= maxL;
        sum += max(0.0, dot(pl, n)) * 1.0 * inv_dist2 * calc_shadow(p, L, maxL);
    }
    sum /= float(num_lights);
    return (sum + indirect) * albedo;
}
