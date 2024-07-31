const vec3 light = vec3(0.0, 0.6, 0.0);

float calc_direct_lighting(vec3 p, vec3 n) {
	const vec3 pl = normalize(light - p);
	float inv_dist2 = 1.0 / distance(p, light);
	inv_dist2 *= inv_dist2;

	return max(0.0, dot(pl, n)) * inv_dist2 * 0.7 /* hardcoded strength */;
}
