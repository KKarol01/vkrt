#include "./common.hlsli"

float4 main(VS_OUT input) : SV_Target0
{
    return input.color;
}