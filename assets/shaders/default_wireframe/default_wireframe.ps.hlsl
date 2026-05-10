#include "./common.hlsli"

float4 main(VS_OUT input) : SV_Target0
{
    return input.color * float4(1.0f, 1.0f, 1.0f, 1.0f);
}