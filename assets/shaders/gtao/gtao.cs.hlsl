#include "./common.hlsli"
#include "./assets/shaders/common.hlsli"
#include "./assets/shaders/util.hlsli"

#define LOCAL_SIZE 8

float GTAO(
    Texture2D<float> depthTex,
    float3 normal,
    SamplerState samp,
    float2 uv)
{
    const int SSAO_SAMPLES = 8;
    const int NUM_DIRECTIONS = 8;

    float3 centerPos = unproject_inf_revz_depth(uv, depthTex);

    float3 viewV = normalize(-centerPos);

    float2 projScale = float2(
        get_gsb(GPUEngConstants, 0).proj[0][0],
        get_gsb(GPUEngConstants, 0).proj[1][1]);

    float2 radius = projScale * get_gsb(GPUEngAOSettings, 0).radius / abs(centerPos.z);

    float visibility = 0.0;

    for(int dirIdx = 0; dirIdx < NUM_DIRECTIONS; dirIdx++)
    {
        float phi = (ENG_PI / NUM_DIRECTIONS) * dirIdx;
        float2 dir = float2(cos(phi), sin(phi));

        float3 dir3 = float3(dir, 0);

        float3 ortho = normalize(dir3 - dot(dir3, viewV) * viewV);
        float3 axis  = normalize(cross(dir3, viewV));

        float3 projNormal = normal - axis * dot(normal, axis);
        float projLen = max(length(projNormal), 1e-4);

        float sgn_n = sign(dot(ortho, projNormal));
        float cos_n = saturate(dot(projNormal, viewV) / projLen);
        float n = sgn_n * acos(cos_n);

        float horizonCos[2] = { -1.0, -1.0 };

        for(int side = 0; side < 2; side++)
        {
            for(int i = 1; i <= SSAO_SAMPLES; i++)
            {
                float s = (i + 0.5) / SSAO_SAMPLES;

                float2 offset = dir * radius;
                float2 sampleUV = uv + (side == 0 ? -1.0 : 1.0) * s * offset;

                float3 samplePos = unproject_inf_revz_depth(sampleUV, depthTex);

                float3 v = normalize(samplePos - centerPos);

                horizonCos[side] = max(horizonCos[side], dot(v, viewV));
            }
        }

        float h1 = n + clamp(-acos(horizonCos[0]) - n, -ENG_HALF_PI, ENG_HALF_PI);
        float h2 = n + clamp( acos(horizonCos[1]) - n, -ENG_HALF_PI, ENG_HALF_PI);

        visibility += projLen * (
            cos_n
            + 2.0 * h1 * sin(n)
            - cos(2.0 * h1 - n)
            + cos_n
            + 2.0 * h2 * sin(n)
            - cos(2.0 * h2 - n)
        ) * 0.25;
    }

    return saturate(visibility / NUM_DIRECTIONS);
}

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    Texture2D<float> in_depth = gTexture2Df1s[pc.DepthTextureIndex];
	RWTexture2D<float4> in_normal = gRWTexture2Df4s[pc.NormalImageIndex];
	RWTexture2D<float4> out_ao = gRWTexture2Df4s[pc.AOImageIndex];
	
	uint2 out_size;
	out_ao.GetDimensions(out_size.x, out_size.y);
	if(any(thread_id.xy >= out_size)) { return; }
	
	const float2 uv = float2(thread_id.xy + 0.5) / float2(out_size.xy);
	const float3 normal_v = -in_normal[thread_id.xy].xyz;
	const float3 pos_v = unproject_inf_revz_depth(uv, in_depth);
	const float gtao = GTAO(in_depth, in_normal[thread_id.xy].xyz, gSamplerStates[ENG_SAMPLER_NEAREST], uv);
	
	out_ao[thread_id.xy] = float4(gtao.xxx, 1.0);
} 