#if 0
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

// Transforms grid coord [0, num_probes] into world space position with dynamic offsets
vec3 grid_coord_to_position_offset(ivec3 grid_coords, int probe_idx) {
	const int probe_counts_xy = int(ddgi.probe_counts.x * ddgi.probe_counts.y);
	const ivec2 offset_coords = ivec2(probe_idx % probe_counts_xy, probe_idx / probe_counts_xy);
	return grid_coord_to_position(grid_coords) + imageLoad(ddgi_probe_offset_image, offset_coords).xyz;
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
#if 0
	const int probe_with_border = probe_res + 2;
	const ivec3 pc = ivec3(ddgi.probe_counts.xyz);
	const int x = (coords.x / probe_with_border) % pc.x;
	const int y = (coords.x / (probe_with_border * pc.x)) % pc.y;
	const int z = (coords.y / probe_with_border) % pc.z;
	return x + y*pc.x + z*pc.x*pc.y;
#else 
	const int probe_with_border = probe_res + 2;
	const int tex_width = int(ddgi.probe_counts.x * ddgi.probe_counts.y) * probe_with_border;
	const int probes_per_side = tex_width / probe_with_border;
	return int(coords.x / probe_with_border) + probes_per_side * int(coords.y / probe_with_border);
#endif
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
#else 
// Utility methods ///////////////////////////////////////////////////////
vec3 spherical_fibonacci(float i, float n) {
    const float PHI = sqrt(5.0f) * 0.5 + 0.5;
#define madfrac(A, B) ((A) * (B)-floor((A) * (B)))
    float phi       = 2.0 * PI * madfrac(i, PHI - 1);
    float cos_theta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
    float sin_theta = sqrt(clamp(1.0 - cos_theta * cos_theta, 0.0f, 1.0f));

    return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

#undef madfrac
}


float sign_not_zero(in float k) {
    return (k >= 0.0) ? 1.0 : -1.0;
}

vec2 sign_not_zero2(in vec2 v) {
    return vec2(sign_not_zero(v.x), sign_not_zero(v.y));
}

// Assumes that v is a unit vector. The result is an octahedral vector on the [-1, +1] square.
vec2 oct_encode(in vec3 v) {
    float l1norm = abs(v.x) + abs(v.y) + abs(v.z);
    vec2 result = v.xy * (1.0 / l1norm);
    if (v.z < 0.0) {
        result = (1.0 - abs(result.yx)) * sign_not_zero2(result.xy);
    }
    return result;
}


// Returns a unit vector. Argument o is an octahedral vector packed via oct_encode,
// on the [-1, +1] square
vec3 oct_decode(vec2 o) {
    vec3 v = vec3(o.x, o.y, 1.0 - abs(o.x) - abs(o.y));
    if (v.z < 0.0) {
        v.xy = (1.0 - abs(v.yx)) * sign_not_zero2(v.xy);
    }
    return normalize(v);
}

// Compute normalized oct coord, mapping top left of top left pixel to (-1,-1) and bottom right to (1,1)
vec2 normalized_oct_coord(ivec2 fragCoord, int probe_side_length) {

    int probe_with_border_side = probe_side_length + 2;
    vec2 octahedral_texel_coordinates = ivec2((fragCoord.x - 1) % probe_with_border_side, (fragCoord.y - 1) % probe_with_border_side);

    octahedral_texel_coordinates += vec2(0.5f);
    octahedral_texel_coordinates *= (2.0f / float(probe_side_length));
    octahedral_texel_coordinates -= vec2(1.0f);

    return octahedral_texel_coordinates;
}

vec2 get_probe_uv(vec3 direction, int probe_index, int full_texture_width, int full_texture_height, int probe_side_length) {

    // Get octahedral coordinates (-1,1)
    const vec2 octahedral_coordinates = oct_encode(normalize(direction));
    // TODO: use probe index for this.
    const float probe_with_border_side = float(probe_side_length) + 2.0f;
    const int probes_per_row = (full_texture_width) / int(probe_with_border_side);
    // Get probe indices in the atlas
    ivec2 probe_indices = ivec2((probe_index % probes_per_row), 
                               (probe_index / probes_per_row));
    
    // Get top left atlas texels
    vec2 atlas_texels = vec2( probe_indices.x * probe_with_border_side, probe_indices.y * probe_with_border_side );
    // Account for 1 pixel border
    atlas_texels += vec2(1.0f);
    // Move to center of the probe area
    atlas_texels += vec2(probe_side_length * 0.5f);
    // Use octahedral coordinates (-1,1) to move between internal pixels, no border
    atlas_texels += octahedral_coordinates * (probe_side_length * 0.5f);
    // Calculate final uvs
    const vec2 uv = atlas_texels / vec2(float(full_texture_width), float(full_texture_height));
    return uv;
}

vec2 texture_coord_from_direction(vec3 dir, int probe_index, int full_texture_width, int full_texture_height, int probe_side_length) {
    // Get encoded [-1,1] octahedral coordinate
    vec2 normalized_oct_coord = oct_encode(normalize(dir));
    // Map it to [0,1]
    vec2 normalized_oct_coord_zero_one = (normalized_oct_coord * 0.5) + 0.5f;

    // Length of a probe side, plus one pixel on each edge for the border
    float probe_with_border_side = float(probe_side_length) + 2.0f;

    vec2 oct_coord_normalized_to_texture_dimensions = (normalized_oct_coord_zero_one * float(probe_side_length)) 
                                                    / vec2(float(full_texture_width), float(full_texture_height));

    int probes_per_row = (full_texture_width) / int(probe_with_border_side);

    // Add (1,1) back to texCoord within larger texture. Compensates for 1 pix border around top left probe.
    vec2 probe_top_left_position = vec2((probe_index % probes_per_row) * probe_with_border_side,
        (probe_index / probes_per_row) * probe_with_border_side) + vec2(1.0f, 1.0f);

    vec2 normalized_probe_top_left_position = vec2(probe_top_left_position) / vec2(float(full_texture_width), float(full_texture_height));

    return vec2(normalized_probe_top_left_position + oct_coord_normalized_to_texture_dimensions);
}

// Probe coordinate system ///////////////////////////////////////////////
ivec3 probe_index_to_grid_indices( int probe_index ) {
    const int probe_x = probe_index % int(ddgi.probe_counts.x);
    const int probe_counts_xy = int(ddgi.probe_counts.x * ddgi.probe_counts.y);

    const int probe_y = (probe_index % probe_counts_xy) / int(ddgi.probe_counts.x);
    const int probe_z = probe_index / probe_counts_xy;

    return ivec3( probe_x, probe_y, probe_z );
}

int probe_indices_to_index(in ivec3 probe_coords) {
    return int(probe_coords.x + probe_coords.y * ddgi.probe_counts.x + probe_coords.z * ddgi.probe_counts.x * ddgi.probe_counts.y);
}

vec3 grid_indices_to_world_no_offsets( ivec3 grid_indices ) {
    return grid_indices * ddgi.probe_walk + ddgi.probe_start;
}

vec3 grid_indices_to_world( ivec3 grid_indices, int probe_index ) {
    const int probe_counts_xy = int(ddgi.probe_counts.x * ddgi.probe_counts.y);
    ivec2 probe_offset_sampling_coordinates = ivec2(probe_index % probe_counts_xy, probe_index / probe_counts_xy);

    vec3 probe_offset = imageLoad(ddgi_probe_offset_image, probe_offset_sampling_coordinates).rgb;

    return grid_indices_to_world_no_offsets( grid_indices ) + probe_offset;
}

ivec3 world_to_grid_indices( vec3 world_position ) {
    return clamp(ivec3((world_position - ddgi.probe_start) / vec3(ddgi.probe_walk)), ivec3(0), ivec3(ddgi.probe_counts) - ivec3(1));
}

int get_probe_index_from_pixels(ivec2 pixels, int probe_with_border_side, int full_texture_width) {
    int probes_per_side = full_texture_width / probe_with_border_side;
    return int(pixels.x / probe_with_border_side) + probes_per_side * int(pixels.y / probe_with_border_side);
}

#endif
