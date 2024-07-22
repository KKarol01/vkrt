const vec3 light = vec3(0.0, 0.0, 1.0);

float calc_direct_lighting(vec3 p, vec3 n) {
	const vec3 pl = normalize(light - p);
	const float dist2 = distance(p, light);

	return max(0.0, dot(pl, n)) / dist2;
}
