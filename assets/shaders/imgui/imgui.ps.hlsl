#include "imgui.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUImGuiParams)

float4 main(VS_OUT input) : SV_Target0
{
    return input.color * pc.texture.get().Sample(gSamplerLinearClamp, input.uv);
}
