#include "meshpass.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUMeshPassParams)

struct FSOutput
{
	float4 out_color : SV_Target0;
	float4 out_normal : SV_Target2;
};

FSOutput main(VSOutput vsoutput)
{
    FSOutput fsoutput;
	
	const GPUEngConstants consts = pc.engine.load();
	const GPUMaterial mat = consts.materials.load(vsoutput.material_index);

    float4 base_color = mat.base_color.get_nonuniform().Sample(gSamplerLinear, vsoutput.uv);
    base_color *= unpack_unorm4x8(mat.base_color_factor);
	if(base_color.a < 0.5) { discard; } 

	fsoutput.out_color = base_color;
    fsoutput.out_normal = vsoutput.normal;
    return fsoutput;
}
