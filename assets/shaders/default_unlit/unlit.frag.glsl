#version 460

#include "./default_unlit/common.inc.glsl"

layout(location = 0) in VsOut {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    flat uint32_t instance_index;
    vec3 water_normal;
}
fsin;

layout(location = 0) out vec4 OUT_COLOR;
// Constants (tweak these values as needed)
const vec3 LIGHT_DIR             = normalize(vec3(0.5, 1.0, 0.3));
const vec3 WATER_COLOR           = vec3(0.0, 0.3, 0.6);
const vec3 SKY_COLOR             = vec3(0.6, 0.8, 1.0);
const float SHININESS            = 64.0;
const float FRESNEL_POWER        = 5.0;
const float SPECULAR_INTENSITY   = 1.0;
const float TRANSMISSION_INTENSITY = 0.5;
const float REFRACT_IOR          = 1.33; // approximate water IOR

void main() {
    // Compute view direction per-fragment
    vec3 viewDir = normalize(constants.cam_pos - fsin.position);

    // Reconstruct normal from world position derivatives
    vec3 dx = dFdx(fsin.position);
    vec3 dy = dFdy(fsin.position);
    vec3 normal = normalize(cross(dx, dy));
    if (dot(normal, viewDir) < 0.0) normal = -normal;

    // Fresnel term (Schlick's approximation)
    float cosTheta = clamp(dot(normal, viewDir), 0.0, 1.0);
    float fresnel   = pow(1.0 - cosTheta, FRESNEL_POWER);

    // Diffuse component (minimal)
    float lambert = max(dot(normal, LIGHT_DIR), 0.0);
    vec3 diffuse  = WATER_COLOR * lambert;

    // Specular component (Blinn-Phong)
    vec3 halfDir  = normalize(LIGHT_DIR + viewDir);
    float spec    = SPECULAR_INTENSITY * pow(max(dot(normal, halfDir), 0.0), SHININESS);

    // Sky reflection via Fresnel blend
    vec3 skyReflect = mix(diffuse + spec * vec3(1.0), SKY_COLOR, fresnel);

    // Diffuse transmission (approximate subsurface scattering)
    vec3 refractDir = refract(-viewDir, normal, 1.0 / REFRACT_IOR);
    float transAmt  = TRANSMISSION_INTENSITY * (1.0 - lambert);
    vec3 transmit  = WATER_COLOR * transAmt;

    // Combine reflection and transmission
    vec3 color = mix(transmit, skyReflect, fresnel);

    OUT_COLOR = vec4(color, 1.0);
}
