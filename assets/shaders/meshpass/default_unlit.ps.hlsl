#include "common.hlsli"

struct FSOutput
{
	float4 color0 : SV_Target0;
};

FSOutput main(VSOutput vsoutput)
{
    FSOutput fsoutput;
	
	float fmaterialid = 1.0 / log2(float(vsoutput.material_index));
	
	GPUEngConstants constants = get_gsb2(GPUEngConstants, pc, 0);
    
    float4 base_color = float4(1.0, 1.0, 1.0, 1.0);
	GPUMaterial mat = get_gsb2(GPUMaterial, constants, vsoutput.material_index);
    Texture2D<float4> base_color_tex = gTexture2Df4s[NonUniformResourceIndex(mat.base_color_idx)];
    base_color = base_color_tex.Sample(gSamplerStates[ENG_SAMPLER_LINEAR], vsoutput.uv);
    base_color *= unpack_unorm4x8(mat.base_color_factor);
	if(base_color.a < 0.5) { discard; } 
	fsoutput.color0 = base_color; 
	//fsoutput.color0 = fmaterialid;
	// fsoutput.color0 = fmaterialid;
	//fsoutput.color0 = float4(vsoutput.uv, 0.0, 1.0);

	return fsoutput;
}