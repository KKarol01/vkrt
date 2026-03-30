#ifndef SSAO_COMMON_H
#define SSAO_COMMON_H

#include "./assets/shaders/common.hlsli"

#ifndef NO_PUSH_CONSTANTS
struct PushConstants
{
    ENG_TYPE_UINT GPUEngConstantsBufferIndex;
    ENG_TYPE_UINT DepthTextureIndex;
    ENG_TYPE_UINT NormalImageIndex;
    ENG_TYPE_UINT AOImageIndex;
};
[[vk::push_constant]] PushConstants pc;
#endif

/*
Generated with:
    glm::vec3 ssaopoints[64];
    std::default_random_engine eng;
    std::uniform_real_distribution<float> fdist(0.0f, 1.0f);
    for(int i = 0; i < 64; ++i)
    {
        const auto u = fdist(eng);
        const auto v = fdist(eng);
        const auto z = v;
        const auto r = glm::sqrt(1.0f - v * v);
        ssaopoints[i] = glm::vec3{
            r * glm::cos(2.0f * glm::pi<float>() * u),
            r * glm::sin(2.0f * glm::pi<float>() * u),
            z,
        };
        const auto lerp = [](auto a, auto b, auto t) { return a + t * (b - a); };
        const auto scale = (float)i / 64.0f;
        ssaopoints[i] *= lerp(0.1f, 1.0f, scale * scale);
    }
*/
static const float3 SSAO_SAMPLES[64] = {
    float3(0.04, -0.09, 0.01),
    float3(0.05, -0.03, 0.08),
    float3(0.02, 0.02, 0.10),
    float3(0.09, -0.05, 0.02),
    float3(-0.07, -0.07, 0.03),
    float3(0.07, 0.05, 0.06),
    float3(-0.02, 0.10, 0.02),
    float3(-0.01, -0.00, 0.11),
    float3(0.01, -0.00, 0.11),
    float3(0.03, -0.01, 0.11),
    float3(0.05, 0.07, 0.09),
    float3(0.02, -0.00, 0.12),
    float3(0.13, -0.03, 0.01),
    float3(-0.08, 0.01, 0.11),
    float3(0.04, -0.13, 0.04),
    float3(0.09, 0.12, 0.00),
    float3(-0.14, 0.07, 0.02),
    float3(0.11, -0.06, 0.10),
    float3(0.02, -0.08, 0.15),
    float3(0.15, -0.04, 0.09),
    float3(-0.06, -0.09, 0.15),
    float3(0.18, 0.04, 0.07),
    float3(0.12, -0.16, 0.04),
    float3(0.14, -0.06, 0.15),
    float3(-0.09, -0.19, 0.09),
    float3(0.01, -0.16, 0.18),
    float3(-0.01, -0.22, 0.12),
    float3(-0.18, 0.15, 0.11),
    float3(-0.15, -0.22, 0.05),
    float3(0.13, 0.24, 0.09),
    float3(-0.05, -0.17, 0.24),
    float3(0.29, 0.06, 0.10),
    float3(-0.03, 0.16, 0.28),
    float3(0.32, 0.10, 0.05),
    float3(0.03, 0.02, 0.35),
    float3(0.09, -0.19, 0.30),
    float3(-0.13, -0.36, 0.05),
    float3(-0.11, 0.24, 0.31),
    float3(0.35, -0.11, 0.20),
    float3(0.32, 0.07, 0.29),
    float3(-0.42, 0.17, 0.06),
    float3(-0.34, 0.31, 0.10),
    float3(0.05, -0.48, 0.02),
    float3(0.14, -0.49, 0.02),
    float3(0.19, 0.44, 0.21),
    float3(-0.48, 0.03, 0.25),
    float3(-0.46, 0.17, 0.28),
    float3(-0.22, -0.28, 0.46),
    float3(-0.06, -0.23, 0.56),
    float3(0.01, -0.37, 0.51),
    float3(-0.07, 0.45, 0.46),
    float3(-0.29, -0.61, 0.00),
    float3(-0.27, -0.40, 0.49),
    float3(0.29, 0.47, 0.46),
    float3(0.48, 0.45, 0.34),
    float3(-0.48, 0.00, 0.59),
    float3(0.63, -0.16, 0.45),
    float3(-0.21, 0.33, 0.71),
    float3(-0.42, -0.25, 0.68),
    float3(0.14, 0.85, 0.02),
    float3(0.00, -0.51, 0.73),
    float3(-0.02, 0.52, 0.75),
    float3(-0.32, -0.01, 0.89),
    float3(-0.28, -0.84, 0.40),
};

#endif