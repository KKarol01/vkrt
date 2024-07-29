float signNotZero(float k) {
    return k >= 0.0 ? 1.0 : -1.0;
}

vec2 signNotZero(vec2 v) {
    return vec2( signNotZero(v.x), signNotZero(v.y) );
}

// Normalized direction vector into [-1, +1] octahedral mapped one
vec2 octEncode(in vec3 v) {
    float l1norm = abs(v.x) + abs(v.y) + abs(v.z);
    vec2 result = v.xy * (1.0/l1norm);
    if (v.z < 0.0) {
        result = (1.0 - abs(result.yx)) * signNotZero(result.xy);
    }
    return result;
}

// Decode octahedral mapped vector back into normalized direction vector
vec3 octDecode(vec2 p) {
	float x = p.x, y = p.y;
    vec3 v = vec3(x, y, 1.0 - abs(x) - abs(y));
    if (v.z < 0) {
        v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    }
    return normalize(v);
}

// Spherical fibonnaci normalized vector distribution
vec3 sphericalFibonacci(float i, float n) {
    const float PHI = sqrt(5.0) * 0.5 + 0.5;
#   define madfrac(A, B) ((A)*(B)-floor((A)*(B)))
    float phi = 2.0 * PI * madfrac(i, PHI - 1);
    float cosTheta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));

    return vec3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta);

#   undef madfrac
}

// Maps the result of octahedral encoding from [-1, +1] to [0, probe_resolution-1]
vec2 normalToUvRectOct(vec3 normal, int probe_res){
#if 1
    vec2 p = octEncode(normal);
	return (p * 0.5 + 0.5) * float(probe_res);
#else
	float l1norm = abs(normal.x) + abs(normal.y) + abs(normal.z); 
	vec2 res = normal.xy * (1.0 / l1norm);
	if(normal.z < 0.0) {
		res = (1.0 - abs(res.yx)) * signNotZero(res.xy);
	}
	return res;
#endif
}

// Unmaps from [0, probe_resolution-1] into [-1, +1] spherical direction vector
vec3 uvRectOctToNormal(vec2 normal, int probe_res) {
#if 0
	vec2 orig = normal.xy;
    float sum = dot(vec2(1.0), abs(normal.xy));

    if(sum > 1.0) {
        normal.xy = (1.0 - abs(normal.yx)) * sign(orig.xy);
    }
    float z = 1.0 - sum;
    
    return normalize(vec3(normal.xy, z));
#else 
    normal = (normal / probe_res) * 2.0 - 1.0;
    vec3 p = octDecode(normal);
	return p;
#endif
}

// Transforms texel ivec2 coords into [-1, +1] octahedral coords
vec2 normalizedOctCoord(ivec2 coords, int probe_res) {	
	// this function should be called only when coords are not representing
	// border texels, and it assumes one texel border width around probe data
	// i.e. (x, y) == (1, 1) means bottom left texel that is non-border, so
	// -1 is applied to make it (0, 0)

	int probe_with_border = probe_res + 2;

#if 0
	float x = float((coords.x - 1) % probe_with_border) + 0.5;
	float y = float((coords.y - 1) % probe_with_border) + 0.5;
	return (vec2(x, y) / float(probe_res - 1)) * 2.0 - 1.0;
#else 
	vec2 octFragCoord = ivec2( (coords - ivec2(1)) % probe_with_border );
	return (vec2(octFragCoord) + vec2(0.5)) * (2.0 / float(probe_res)) - vec2(1.0);
#endif
}

// Transforms grid coord [0, num_probes] into world space position
vec3 grid_coord_to_position(ivec3 grid_coords) {
    return ddgi.probe_start + vec3(grid_coords) * ddgi.probe_walk;
}

// Transforms probe id from [0, total_num_probes] into grid coords [0, num_probes]
ivec3 get_probe_grid_coords(int probe_id) {
#if 0
    ivec3 pos;
    pos.x = probe_id & (int(ddgi.probe_counts.x) - 1);
    pos.y = (probe_id & (int(ddgi.probe_counts.x) * int(ddgi.probe_counts.y) - 1)) >> findMSB(ddgi.probe_counts.x);
    pos.z = probe_id >> findMSB(ddgi.probe_counts.x * ddgi.probe_counts.y);
    return pos;
#else 
	ivec3 pos;
	pos.x = probe_id % int(ddgi.probe_counts.x);
	pos.y = (probe_id % int(ddgi.probe_counts.x*ddgi.probe_counts.y)) / int(ddgi.probe_counts.x);
	pos.z = probe_id / int(ddgi.probe_counts.x*ddgi.probe_counts.y);
	return pos;
#endif
}

// Transforms texel coord into probe id (for irradiance texture)
int get_probe_index_from_coords(ivec2 coords, int probe_res) {
	const int probe_with_border = probe_res + 2;
	const ivec3 pc = ivec3(ddgi.probe_counts.xyz);
	const int x = (coords.x / probe_with_border) % pc.x;
	const int y = (coords.x / (probe_with_border * pc.x)) % pc.y;
	const int z = (coords.y / probe_with_border) % pc.z;
	return x + y*pc.x + z*pc.x*pc.y;
} 

// Transforms grid coords into 1d index
int get_probe_index_from_grid_coords(ivec3 grid_coords) {
	return grid_coords.x + grid_coords.y*int(ddgi.probe_counts.x) + grid_coords.z*int(ddgi.probe_counts.x*ddgi.probe_counts.y);
}

// Transforms world pos coords into grid coords
ivec3 world_to_grid_coords(vec3 world_pos) {
	return clamp(
		ivec3((world_pos - ddgi.probe_start) / ddgi.probe_walk),
		ivec3(0),
		ivec3(1)
	);
}

// Gets probe texel vec for irradiance texture from normal spherical vector and probe grid coords
vec2 get_probe_uv(vec3 normal, ivec3 probe_coords, int probe_res) {
#if 0
	const int probe_with_border = int(probe_res) + 2;
	const float irr_width = float(ddgi.probe_counts.x * ddgi.probe_counts.y * probe_with_border);
	const float irr_height = float(ddgi.probe_counts.z * probe_with_border);
	
	vec2 offset = vec2(
		(probe_coords.x + probe_coords.y * ddgi.probe_counts.x) * probe_with_border,
		probe_coords.z * probe_with_border
	);

	return (normalToUvRectOct(normal, probe_res) + offset + vec2(1.0)) / vec2(irr_width, irr_height);
#else 
	const int probe_with_border = probe_res + 2;
	const float tex_width = float(ddgi.probe_counts.x * ddgi.probe_counts.y * probe_with_border);
	const float tex_height = float(ddgi.probe_counts.z * probe_with_border);
	const vec2 oct_coords = octEncode(normalize(normal));
	vec2 uv = vec2((probe_coords.x + probe_coords.y * ddgi.probe_counts.x) * probe_with_border, probe_coords.z * probe_with_border);
	uv += vec2(1.0);
	uv += vec2(probe_res * 0.5);
	uv += oct_coords * (probe_res * 0.5);
	return uv / vec2(tex_width, tex_height);
#endif
}

