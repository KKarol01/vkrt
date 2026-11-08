#include "meshpass.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUMeshPassParams)

struct VSInput
{
	uint vertex_index : SV_VertexID;
	uint instance_index : SV_InstanceID;
};

VSOutput main(VSInput vsinput)
{
	const GPUEngConstants consts = pc.engine.load();
	const GPUInstanceId instanceid = pc.instances.load(vsinput.instance_index);
	const GPUVertexAttribute attribs = consts.vertex_attributes.load(vsinput.vertex_index);
	
    VSOutput vsoutput;
	vsoutput.position = mul(consts.proj_view, float4(consts.vertex_positions.load(vsinput.vertex_index).pos, 1.0));
	vsoutput.normal = float4(attribs.normal, 0.0);
	vsoutput.uv = attribs.uv;
	vsoutput.material_index = instanceid.mati;
    
    return vsoutput;
}
