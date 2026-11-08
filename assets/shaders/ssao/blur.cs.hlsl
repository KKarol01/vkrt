#include "ssao.shared.hlsli"

ENG_PUSH_CONSTANTS(GPUSSAOBlurParams)

static const uint LOCAL_SIZE = 8;

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
	RWTexture2D<float4> in_ao = pc.ao.get();
	RWTexture2D<float4> out_ao = pc.out_blur.get();
	
	uint2 dims;
	in_ao.GetDimensions(dims.x, dims.y);
	if(any(thread_id.xy >= dims.xy)) { return; }
	
	float4 sum = 0;
    [unroll]
    for (int x = -2; x < 2; ++x)
    {
        [unroll]
        for (int y = -2; y < 2; ++y)
        {
            int2 sample_pos = clamp(int2(thread_id.xy) + int2(x, y), 0, int2(dims) - 1);
            sum += in_ao[sample_pos];
        }
    }
	out_ao[thread_id.xy] = sum / 16.0;
}
