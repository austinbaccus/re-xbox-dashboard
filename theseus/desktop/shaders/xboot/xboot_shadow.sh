// The shadow of the chamber (GPU.md 4.4): the depth of the light view,
// filtered, against t.z / t.w. The light view is s_light, which the
// including shader declares. It is addressed in texels and has a white
// border.

float light_tap(vec2 p)
{
	ivec2 q = ivec2(p);
	if (q.x < 0 || q.y < 0 || q.x >= 512 || q.y >= 512) return 1.0;
	return texelFetch(s_light, q, 0).x;
}

float shadow(vec4 t)
{
	if (u_misc.w == 0.0) return 1.0;
	// t.w is max(w, 0): behind the light it is 0, the place lies outside
	// the texture, whose border is the far depth, and z / w is the nearest
	// or the farthest depth by the sign of z
	if (t.w <= 0.0) return t.z < 0.0 ? 1.0 : 0.0;
	vec2 p = t.xy / t.w - 0.5;
	vec2 i = floor(p);
	vec2 f = p - i;
	float stored = mix(mix(light_tap(i), light_tap(i + vec2(1.0, 0.0)), f.x),
	                   mix(light_tap(i + vec2(0.0, 1.0)), light_tap(i + vec2(1.0, 1.0)), f.x), f.y) * 65535.0;
	return stored > clamp(t.z / t.w, 0.0, 65535.0) ? 1.0 : 0.0;
}
