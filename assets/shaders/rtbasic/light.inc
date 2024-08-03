
float time = float(ddgi.frame_num) / 100.0;
	vec3 off = vec3(
		3.0 * cos(time),
		0.0,
		-sin(time)
	);
vec3 light = off + vec3(0.0, 3.2, 0.0);

float calc_direct_lighting(vec3 p, vec3 n) {
	

	vec3 l = light + off;
	const vec3 pl = normalize(l - p);
	float inv_dist2 = 1.0 / distance(p, l);
	//inv_dist2 *= inv_dist2;

	return max(0.0, dot(pl, n)) * inv_dist2 * 10.0 /* hardcoded strength */;
}
