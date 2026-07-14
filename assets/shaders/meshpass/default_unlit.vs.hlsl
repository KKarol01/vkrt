#include "common.hlsli"

struct VSInput
{
	uint vertex_index : SV_VertexID;
	uint instance_index : SV_InstanceID;
};

VSOutput main(VSInput vsinput)
{
	GPUEngConstants constants = get_grwb2(GPUEngConstants, pc, 0);
	GPUInstanceId instanceid = get_grwb2(GPUInstanceId, pc, vsinput.instance_index);
	
    VSOutput vsoutput;
	vsoutput.wpos = get_grwb2(GPUVertexPosition, constants, vsinput.vertex_index).pos.xyz;
	vsoutput.position = mul(constants.proj_view, float4(vsoutput.wpos, 1.0));
	vsoutput.normal = get_grwb2(GPUVertexAttribute, constants, vsinput.vertex_index).normal.xyz;
	vsoutput.uv = get_grwb2(GPUVertexAttribute, constants, vsinput.vertex_index).uv.xy;
	vsoutput.material_index = instanceid.mati;
    
    return vsoutput;
}