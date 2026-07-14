#include "common.hlsli"
#define LOCAL_SIZE 8

static const uint N_SECTORS = 32u;

// Paper
// https://arxiv.org/abs/2301.11376
// Code adjusted from
// https://cybereality.com/screen-space-indirect-lighting-with-visibility-bitmask-improvement-to-gtao-ssao-real-time-ambient-occlusion-algorithm-glsl-shader-implementation/

uint count_ones(uint value)
{
    return countbits(value);
}

uint update_sectors(float min_horizon, float max_horizon, uint bitfield)
{
    uint first_bit = uint(min_horizon * float(N_SECTORS));
    uint horizon_angle = uint(ceil((max_horizon - min_horizon) * float(N_SECTORS)));
    uint bit_span = horizon_angle > 0u ? uint(0xFFFFFFFFu >> (N_SECTORS - horizon_angle)) : 0u;
    uint cur_bitfield = bit_span << first_bit;
    return bitfield | cur_bitfield;
}

float Halton(uint index, uint base)
{
    float f = 1.0f;
    float result = 0.0f;
    uint current = index;
    
    while (current > 0u)
    {
        f = f / float(base);
        result += f * float(current % base);
        current = current / base;
    }
    
    return result;
}

float hash13(float3 p3)
{
    p3  = frac(p3 * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

static bool outputNormals     = false;
static bool outputDirectIllum = false;
static bool outputBounce      = false;

[numthreads(LOCAL_SIZE, LOCAL_SIZE, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint2 dims;
    gOutAOImage.GetDimensions(dims.x, dims.y);
    const GPUEngConstants consts = get_grwb(GPUEngConstants, 0);
    const GPUEngConstants prev_consts = get_grwbi(GPUEngConstants, pc.PrevGPUEngConstantsBufferIndex, 0);
    float2 thread_uv = (float2(dtid.xy) + 0.5f) / float2(dims.xy);
    
    if(any(dtid.xy >= dims)) { return; }
    
    float visibility = 0.0f;
    float3 lighting = 0.0f;
    float2 frontBackHorizon = 0.0;
    
    const float depth = gDepthTexture.SampleLevel(gSamplerNearest, thread_uv, 0).x;
    const float3 position = depth_to_view_pos(dtid.xy, dims, depth);
    
    const float3 world_normal = get_gt(Normal).SampleLevel(gSamplerNearest, thread_uv, 0).xyz;
    float3 normal = normalize(mul(consts.view, float4(world_normal, 0.0)).xyz);
	//normal = calculate_normal_from_depth(dtid.xy, dims.xy, gDepthTexture);
    
    const float3 camera = normalize(-position); 
    
    const float sample_radius = 8.0;
    const float n_slices = 16.0;
    const float n_samples = 64.0;    
    const float hitThickness = 1.0;
    
	if (outputNormals) 
    {
        // Map normal from [-1, 1] to [0, 1] for visual debugging
        gOutAOImage[dtid.xy] = float4(normal, 1.0);
        //gOutAOImage[dtid.xy] = float4(normalize(world_normal), 1.0);
        return; 
    }
	
	if (outputDirectIllum) 
    {
        // Point light at the center of the world, transformed to View Space
        float3 lightPosWorld = float3(0.0, 0.8, 0.0);
        float3 lightPosViewSpace = mul(consts.view, float4(lightPosWorld, 1.0)).xyz;
        
        float3 lightColor = float3(1.0, 1.0, 1.0);
        float lightIntensity = 0.5; // Tweak this based on your scene scale
        
        // Calculate vector and distance from surface to light
        float3 lightVec = lightPosViewSpace - position;
        float dist = length(lightVec);
        float3 lightDir = lightVec / dist;
        
        // Inverse-square attenuation (the +1.0 prevents division by zero artifacts close to the light)
        float attenuation = lightIntensity / (dist * dist + 1.0);
        
        // Diffuse
        float diff = max(dot(normal, lightDir), 0.0);
        float3 diffuse = diff * lightColor;
        
        // Specular
        float3 reflectDir = reflect(-lightDir, normal);
        float spec = pow(max(dot(camera, reflectDir), 0.0), 64.0);
        float3 specular = spec * lightColor;
        
        // Apply attenuation
        float3 finalColor = (diffuse + specular) * attenuation;
        
        gOutAOImage[dtid.xy] = float4(finalColor, 1.0);
        return;
    }
	
if (outputBounce) 
    {
        // Settings
        const uint NUM_RAYS = 8;        // Ray count per pixel (keep low for real-time)
        const uint MAX_STEPS = 16;      // Steps per ray
        const float STEP_SIZE = 0.2;    // View-space distance per step
        const float HIT_THICKNESS = 0.3;// Acceptable Z-depth difference for a hit
        
        float3 accumulatedColor = float3(0.0, 0.0, 0.0);
        float accumulatedVisibility = 0.0;
        
        // Build Tangent Basis (TBN) around the surface normal to orient our hemisphere
        float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
        float3 tangent = normalize(cross(up, normal));
        float3 bitangent = cross(normal, tangent);
        
        // Base spatial noise to randomize ray generation across neighboring pixels
        float spatialSeed = IGN(int(dtid.x), int(dtid.y));

        for (uint r = 0; r < NUM_RAYS; r++)
        {
            // Generate two pseudo-random numbers [0, 1] for this specific ray
            float2 u;
            u.x = hash13(float3(dtid.xy, r + spatialSeed * 100.0));
            u.y = hash13(float3(dtid.xy, r + 50.0 + spatialSeed * 100.0));
            
            // Generate Cosine-Weighted Hemisphere Sample
            // This naturally clusters rays closer to the normal where they contribute more
            float radius = sqrt(u.x);
            float theta = u.y * ENG_TWO_PI;
            
            float3 localDir;
            localDir.x = radius * cos(theta);
            localDir.y = radius * sin(theta);
            localDir.z = sqrt(max(0.0, 1.0 - u.x)); // Z is up in local space
            
            // Transform local ray direction to View Space
            float3 rayDir = localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
            
            // Raymarch setup
            bool hit = false;
            float3 hitColor = float3(0.0, 0.0, 0.0);
            
            // Jitter the starting step to hide banding artifacts
            float rayJitter = u.x; 
            
            for (uint i = 1; i <= MAX_STEPS; i++) 
            {
                // Step forward in View Space
                float3 currentRayPos = position + rayDir * STEP_SIZE * (float(i) + rayJitter);
                
                // Project View-Space ray to Clip-Space
                float4 clipPos = mul(consts.proj, float4(currentRayPos, 1.0));
                float2 ndc = clipPos.xy / clipPos.w;
                
                // Convert NDC to UV [0, 1]
                float2 rayUV = float2(ndc.x, -ndc.y) * 0.5 + 0.5;
                
                // Stop tracing if the ray leaves the screen
                if (any(rayUV < 0.0) || any(rayUV > 1.0)) { break; }
                
                // Sample depth and reconstruct hit position
                float sampleDepth = gDepthTexture.SampleLevel(gSamplerNearest, rayUV, 0).x;
                float3 samplePosition = depth_to_view_pos(uint2(rayUV * dims), dims, sampleDepth);
                
                // Depth test: Check if the ray passed behind the geometry surface
                // (Assuming Right-Handed coordinates where Z is negative into the screen)
                if (currentRayPos.z < samplePosition.z && abs(currentRayPos.z - samplePosition.z) < HIT_THICKNESS)
                {
                    hit = true;
                    // Grab the color from the hit pixel
                    hitColor = get_gt(Color).SampleLevel(gSamplerNearest, rayUV, 0).xyz;
                    break;
                }
            }
            
            // Accumulate results
            if (hit)
            {
                // Note: Because we use Cosine-Weighted sampling, the NdotL term is 
                // mathematically baked into the probability density function (PDF). 
                // We just average the samples directly.
                accumulatedColor += hitColor;
            }
            else
            {
                // If we didn't hit anything, the ray flew off into the skybox/void.
                // It contributes to ambient visibility (AO)
                accumulatedVisibility += 1.0;
            }
        }
        
        // Average the rays
        float3 finalBouncedLighting = accumulatedColor / float(NUM_RAYS);
        float finalVisibility = accumulatedVisibility / float(NUM_RAYS);
        
        gOutAOImage[dtid.xy] = float4(finalBouncedLighting, finalVisibility);
        return;
    }
	
	const float2 aspect = float2(dims.yx) / dims.x;
    
    float proj_radius = (sample_radius * consts.proj[0][0]) / -position.z;
    float step_size = proj_radius / (n_samples + 1.0);
    
    // Outer loop covers a full 360 degrees
    float sliceRotation = ENG_TWO_PI / n_slices; 
    
    // High-quality noise
    float frame_seed = float(consts.frame_index % 4);
    float u1 = hash13(float3(dtid.xy, frame_seed));
    float u2 = hash13(float3(dtid.xy, frame_seed + 42.0));
    
    float slice_jitter = u1 - 0.5; 
    float step_jitter  = u2 - 0.5; 
    
    for (float slice = 0; slice < n_slices + 0.5; slice += 1.0) 
    {
        const float phi = sliceRotation * (slice + slice_jitter) + ENG_PI;
        const float2 omega = float2(cos(phi), sin(phi));
        
        // Vulkan UVs: View +Y (Up) maps to UV -Y (lower V value)
        const float2 uv_omega = float2(omega.x, -omega.y);
        
        const float3 direction = float3(omega.x, omega.y, 0.0);
        
        const float3 orthoDirection = direction - dot(direction, camera) * camera;
        const float3 axis = normalize(cross(direction, camera));
        const float3 projNormal = normal - dot(normal, axis) * axis;
        const float projLength = length(projNormal);

        // Calculate normal tilt (n) exactly as CDRIN does
        const float signN = sign(dot(orthoDirection, projNormal));
        const float cosN = clamp(dot(projNormal, camera) / projLength, -1.0, 1.0);
        const float n = signN * acos(cosN);

        uint occlusion = 0u;

        for(float currentSample = 0.0; currentSample < n_samples + 0.5; currentSample += 1.0)
        {
            float sampleStep = (currentSample + step_jitter + 0.5) * step_size + 0.01;
            
            float2 sampleUV = thread_uv - (uv_omega * sampleStep * aspect);
            
            if(any(sampleUV < 0.0) || any(sampleUV > 1.0)) { continue; }
            
            float sampleDepth = gDepthTexture.SampleLevel(gSamplerNearest, sampleUV, 0).x;
            float3 samplePosition = depth_to_view_pos(uint2(sampleUV * dims), dims, sampleDepth);
            float3 sampleDistance = samplePosition - position;
            float sampleLength = length(sampleDistance);
            
            if(sampleLength < 1e-5) { continue; }
            
            float3 sampleHorizon = sampleDistance / sampleLength;
            
            // Calculate front and back horizons exactly as CDRIN does [cite: 104, 119]
            frontBackHorizon.x = dot(sampleHorizon, camera);
            frontBackHorizon.y = dot(normalize(sampleDistance - camera * hitThickness), camera);

            frontBackHorizon = acos(clamp(frontBackHorizon, -1.0, 1.0));
            frontBackHorizon = clamp((frontBackHorizon + n + ENG_HALF_PI) / ENG_PI, 0.0, 1.0);

            // Prevent unsigned integer underflow
            float minHorizon = min(frontBackHorizon.x, frontBackHorizon.y);
            float maxHorizon = max(frontBackHorizon.x, frontBackHorizon.y);
            
            uint indirect = update_sectors(minHorizon, maxHorizon, 0u);
            uint new_bits = indirect & ~occlusion;
            
            // Optimization: Skip texture fetches if light path is blocked
            if(new_bits == 0u) { continue; } 
             
            // Normal unpack (Ensure your G-Buffer values map correctly to [-1, 1] if needed)
            float3 sampleNormal = normalize(mul(consts.view, float4(get_gt(Normal).SampleLevel(gSamplerNearest, sampleUV, 0).xyz, 0.0)).xyz);
            float3 sampleLight = get_gt(Color).SampleLevel(gSamplerNearest, sampleUV, 0).xyz;
            
            // Accumulate exact fraction of un-occluded light 
            float stepVisibility = float(count_ones(new_bits)) / float(N_SECTORS);
            
            lighting += stepVisibility 
                      * sampleLight 
                      * clamp(dot(normal, sampleHorizon), 0.0, 1.0) 
                      * clamp(dot(sampleNormal, -sampleHorizon), 0.0, 1.0); 

            occlusion |= indirect;
        }
        
        visibility += 1.0 - float(count_ones(occlusion)) / float(N_SECTORS);
    }
	
    visibility /= n_slices;
    lighting /= n_slices;
	gOutAOImage[dtid.xy] = float4(lighting.xyz*10.0, visibility);
	return;
    //float4 current_signal = float4(lighting, visibility);

    //RWTexture2D<float4> history_tex = get_grwt2(OutHistory, float4);
    //RWTexture2D<float4> prev_history_tex = get_grwt2(PrevHistory, float4);
    //
    //float4 history = prev_history_tex[dtid.xy].xyzw * (12.0/16.0) + current_signal * (4.0 / 16.0);
    //history_tex[dtid.xy] = history;
    //
    //gOutAOImage[dtid.xy].xyz = (gOutAOImage[dtid.xy].xyz + history.xyz*10.0) * history.w;
}
#if 0
    // =========================================================================
    // SPATIOTEMPORAL ACCUMULATION
    // =========================================================================
    
    // 1. fetch velocity and reproject
    Texture2D<float2> tex_velocity = get_gt2(Velocity, float2);
    float2 velocity = tex_velocity.SampleLevel(gSamplerNearest, thread_uv, 0);
    float2 prev_uv = thread_uv - velocity;
    
    bool valid_history = true;
    
    // Out-of-bounds check
    if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f)) 
    {
        valid_history = false;
    }
    
    // 2. Disocclusion Check (Using WORLD-SPACE for camera-agnostic rejection)
    if (valid_history)
    {
        Texture2D<float> tex_prev_depth = get_gt2(PrevDepth, float); 
        Texture2D<float4> tex_prev_normal = get_gt2(PrevNormal, float4); 

        float prev_depth_val = tex_prev_depth.SampleLevel(gSamplerNearest, prev_uv, 0).x;
        float3 prev_world_normal = tex_prev_normal.SampleLevel(gSamplerNearest, prev_uv, 0).xyz;
        
        float3 prev_view_pos = unproject_inf_revz_depth(prev_uv, tex_prev_depth);
        
        // Transform current and previous View Space positions to World Space
        float3 prev_world_pos = mul(prev_consts.inv_view, float4(prev_view_pos, 1.0)).xyz;
        float3 current_world_pos = mul(consts.inv_view, float4(position, 1.0)).xyz;
        
        // Compare WORLD-SPACE distance (e.g., 0.2 meters threshold)
        if (distance(current_world_pos, prev_world_pos) > 0.2f) 
        {
            valid_history = false;
        }
        // Compare WORLD-SPACE Normals (approx 25 degree threshold)
        else if (dot(world_normal, prev_world_normal) < 1.1f) 
        {
            valid_history = false;
        }
    }
    
    // 3. Accumulate
    float4 output_signal = current_signal;
    float history_length = 1.0f;
    
    RWTexture2D<float> out_history_len = get_grwt2(OutHistoryLen, float);
    
    if (valid_history)
    {
        Texture2D<float4> tex_history_gi = get_gt2(PrevHistory, float4); 
        Texture2D<float> tex_prev_history_len = get_gt2(PrevHistoryLen, float);
        
        float4 history_signal = tex_history_gi.SampleLevel(gSamplerLinear, prev_uv, 0);
        float prev_len = tex_prev_history_len.SampleLevel(gSamplerNearest, prev_uv, 0);
        
        history_length = min(prev_len + 1.0f, 16.0f);
        
        float alpha = 1.0f / history_length;
        output_signal = lerp(history_signal, current_signal, alpha);
    }
    
    // Write out final blended result and history length
    get_grwt2(OutHistory, float4)[dtid.xy] = output_signal;
    out_history_len[dtid.xy] = history_length;
    
    gOutAOImage[dtid.xy].xyz = output_signal.xyz;
    gOutAOImage[dtid.xy].xyz *= output_signal.w;
}
#endif