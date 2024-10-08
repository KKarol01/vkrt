const int num_lights = 3;
const vec3 lights[] = {
	vec3(0.0, 0.5,  0.0),
	vec3(0.0, 0.5, -0.3),
	vec3(0.0, 0.5,  0.3)
};

vec3 calc_direct_lighting(vec3 p, vec3 n) {
	float sum = 0.0;
	
	for(int i=0; i<num_lights; ++i) {
		vec3 pl = normalize(lights[i] - p);
		float inv_dist2 = 1.0 / distance(p, lights[i]);
		inv_dist2 *= inv_dist2;
		sum += max(0.0, dot(pl, n)) * inv_dist2 * 0.5; /* hardcoded strength */
	}
	return vec3(sum / float(num_lights)) * vec3(1.0); // vec3(75.0/255.0, 95.0/255.0, 115.0/255.0);
}
