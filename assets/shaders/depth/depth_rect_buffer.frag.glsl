#version 460 core
#extension GL_EXT_scalar_block_layout : enable

// TODO: rename in/out variables to the same convention and order them by in -> out

layout(binding = 0, set = 0) uniform sampler2D view_positions;
layout(binding = 1, set = 0) uniform sampler2D view_normals;
layout(binding = 2, set = 0) uniform sampler2D depth_buffer;
layout(binding = 3, set = 0) uniform sampler2D color_buffer;

layout(location = 0) in VsOut {
	vec3 pos;
} vs_out;

layout(scalar, push_constant) uniform PushConstants {
	mat4 projection;
};

layout(location = 0) out float fs_out_color;

#if 1
float randf(float x, float y) {
    return mod(52.9829189 * mod(0.06711056 * x + 0.00583715 * y, 1.0), 1.0);
}

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

int xorshift(in int value) {
    // Xorshift*32
    // Based on George Marsaglia's work: http://www.jstatsoft.org/v08/i14/paper
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

float nextFloat(inout int seed) {
    seed = xorshift(seed);
    // FIXME: This should have been a seed mapped from MIN..MAX to 0..1 instead
    return abs(fract(float(seed) / 3141.592653));
}

vec3 nextVec3(inout int seed) {
	return normalize(clamp(vec3(nextFloat(seed), nextFloat(seed), nextFloat(seed)), 0.0, 1.0));
}

vec2 nextVec2(inout int seed) {
	return normalize(clamp(vec2(nextFloat(seed), nextFloat(seed)), 0.0, 1.0));
}

float linearize_depth(float d,float zNear,float zFar) {
    return zNear * zFar / (zFar + d * (zNear - zFar));
}

const float PI = 3.14159265359;
const float TWO_PI = 2.0 * PI;
const float HALF_PI = 0.5 * PI;
const uint sector_count = 32u;

uint count_bits(uint value) {
    value = value - ((value >> 1u) & 0x55555555u);
    value = (value & 0x33333333u) + ((value >> 2u) & 0x33333333u);
    return ((value + (value >> 4u) & 0xF0F0F0Fu) * 0x1010101u) >> 24u;
}

// https://cdrinmatane.github.io/posts/ssaovb-code/
uint updateSectors(float minHorizon, float maxHorizon, uint outBitfield) {
    uint startBit = uint(minHorizon * float(sector_count));
    uint horizonAngle = uint(ceil((maxHorizon - minHorizon) * float(sector_count)));
    uint angleBit = horizonAngle > 0u ? uint(0xFFFFFFFFu >> (sector_count - horizonAngle)) : 0u;
    uint currentBitfield = angleBit << startBit;
    return outBitfield | currentBitfield;
}
#endif

void main() {

#if 0
	int kernelSize = 32;
	float radius = 0.5;
	float bias = 0.025;
    
	vec2 UV = vs_out.pos.xy * 0.5 + 0.5;
    vec3 color = texture(color_buffer, UV).rgb;
	int seed = int(gl_FragCoord.x * gl_FragCoord.y);
    vec3 fragPos = texture(view_positions, UV).xyz;
    vec3 normal = normalize(texture(view_normals, UV).rgb);
    vec3 randomVec = normalize(vec3(nextVec2(seed) * 2.0 - 1.0, 0.0));

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for(int i = 0; i < kernelSize; ++i) {

		vec3 samples = normalize(vec3(nextVec2(seed) * 2.0 - 1.0, nextFloat(seed)));
		samples *= mix(0.1, 1.0, pow(float(i)/float(kernelSize), 2.0));
        
        vec3 samplePos = TBN * samples;
        samplePos = fragPos + samplePos * radius; 
        
        vec4 offset = vec4(samplePos, 1.0);
        offset = projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
		offset.y = 1.0 - offset.y;

        float sampleDepth = texture(view_positions, offset.xy).z;
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;           
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    
    fs_out_color = occlusion;
#else
	vec2 front_back_horizon = vec2(0.0);
	vec2 uv = vs_out.pos.xy * 0.5 + 0.5;
    vec3 color = texture(color_buffer, uv).rgb;
    vec3 position = texture(view_positions, uv).xyz;
    vec3 normal = normalize(texture(view_normals, uv).rgb);
    vec3 camera = normalize(-position);

	float slice_count = 4.0;
	float sample_count = 4.0;
	float sample_radius = 0.2;
	float sample_offset = 0.01;	
	float hit_thickness = 0.25;
	float jitter = randf(gl_FragCoord.x, gl_FragCoord.y) - 0.5;

	float visibility = 0.0;
	uint occlusion = 0;
	uint indirect = 0;
	for(float slice = 0.0; slice < slice_count + 0.5; slice += 1.0) {	
		float phi = (slice + jitter) * TWO_PI / (slice_count - 1.0);
		vec2 omega = vec2(cos(phi), sin(phi));
		vec3 dir = vec3(omega, 0.0);
		vec3 ortho_dir = dir - dot(dir, camera)*camera;
		vec3 axis = cross(dir, camera);
		vec3 proj_normal = normal - dot(normal, axis)*axis;
		float proj_len = length(proj_normal);
		float cos_n = dot(proj_normal / proj_len, camera);
		
		float n = -sign(dot(proj_normal, cross(camera, ortho_dir))) * acos(cos_n);
		
		for(float samp = 0.0; samp < sample_count + 0.5; samp += 1.0) {
			vec4 sample_uv = vec4(position + dir * (samp + jitter) * sample_radius / sample_count + sample_offset, 1.0);
			sample_uv = projection * sample_uv;
			sample_uv /= sample_uv.w;
			sample_uv.xy = sample_uv.xy * 0.5 + 0.5;
			sample_uv.y = 1.0 - sample_uv.y;
			vec3 sample_pos = texture(view_positions, sample_uv.xy).rgb;
			vec3 sample_delta = sample_pos - position;
			float sample_len = length(sample_delta);
			vec3 sample_horizon = sample_delta / sample_len;
			front_back_horizon = vec2(
				dot(sample_horizon, camera),
				dot(normalize(sample_delta - camera * hit_thickness), camera));
			front_back_horizon = acos(front_back_horizon);
			front_back_horizon = clamp((front_back_horizon + n + HALF_PI) / PI, 0.0, 1.0);
			indirect = updateSectors(front_back_horizon.x, front_back_horizon.y, 0);
			occlusion |= indirect;
		}
		visibility += 1.0 - float(count_bits(occlusion)) / float(sector_count);
	}
	visibility /= slice_count;
	fs_out_color = pow(visibility, 1.0);
#endif
}
