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
const vec3 LIGHT_DIR = normalize(vec3(15.0, 80.0, 0.0));
void main() {
    vec3 N = normalize(fsin.water_normal);
    vec3 V = normalize(constants.cam_pos - fsin.position);
    vec3 L = LIGHT_DIR;
    vec3 H = normalize(L + V);

    float h = fsin.position.y;
    float h_min = -3.0;
    float h_max = 6.0;

    float depthFactor = smoothstep(h_min, h_max, h);

    vec3 deepColor = vec3(0.0, 0.02, 0.15);
    vec3 midColor = vec3(0.0, 0.05, 0.2);
    vec3 shallowColor = vec3(0.05, 0.1, 0.25);

    vec3 heightTint = mix(deepColor, midColor, depthFactor);
    heightTint = mix(heightTint, shallowColor, pow(depthFactor, 2.0));

    float ambient = 0.3;
    float diffTerm = max(dot(N, L), 0.3);
    vec3 baseColor = heightTint * (ambient + diffTerm * (1.0 - ambient));

    float shininess = 64.0;
    float specular = pow(max(dot(N, H), 0.0), shininess);

    specular *= mix(1.0, 0.5, 1.0 - depthFactor);
    specular *= (diffTerm + 0.2);

    float F0 = 0.02;
    float fresnelP = 5.0;
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - max(dot(N, V), 0.1), fresnelP);

    vec3 color = baseColor * (1.0 - fresnel) + specular * fresnel;

    OUT_COLOR = vec4(color * 4.0, 1.0);
}