#include "common.hlsli"

struct FSOutput
{
	float4 out_color : SV_Target0;
	float4 out_normal : SV_Target2;
};

FSOutput main(VSOutput vsoutput)
{
    FSOutput fsoutput;
	
	GPUEngConstants constants = get_grwb2(GPUEngConstants, pc, 0);
    
    float4 base_color = float4(1.0, 1.0, 1.0, 1.0); 
	GPUMaterial mat = get_grwb2(GPUMaterial, constants, vsoutput.material_index);
    Texture2D<float4> base_color_tex = gTextures2Dfloat4[NonUniformResourceIndex(mat.base_color_idx)];
    base_color = base_color_tex.Sample(gSamplerStates[ENG_SAMPLER_LINEAR], vsoutput.uv); 
    base_color *= unpack_unorm4x8(mat.base_color_factor);
	if(base_color.a < 0.5) { discard; } 
	
	const float3 light_pos = float3(-5.0, 5.0, 10.0);
	base_color = base_color * clamp(dot(-normalize(light_pos - vsoutput.wpos), vsoutput.normal), 0.0, 1.0);
	
	//fsoutput.color0 = base_color; 
	//fsoutput.color0 = fmaterialid;
	// fsoutput.color0 = fmaterialid;
	//fsoutput.color0 = float4(vsoutput.uv, 0.0, 1.0);

	fsoutput.out_color = base_color;
    fsoutput.out_normal = float4(vsoutput.normal, 0.0); 
    return fsoutput;
}