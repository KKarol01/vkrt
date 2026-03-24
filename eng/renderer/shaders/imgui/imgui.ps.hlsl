#include "./common.hlsli"

float4 main(VS_OUT input) : SV_Target0
{
    return input.color * gTexture2Ds[pc.color_tex].Sample(gSamplerStates[ENG_SAMPLER_LINEAR], input.uv) * float4(1.0, 1.0, 1.0, 1.0);
}