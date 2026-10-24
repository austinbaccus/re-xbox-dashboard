$input v_d0, v_t0, v_t1, v_t2, v_t3

// Combiners from the texture stages (GPU.md 5), u_mode says what they take.
// Filtered between four texels, level 0 alone; where the stage has a border
// colour (u_mode.w) the shader reads the texels itself and takes u_k[2]
// outside the texture.

#include <bgfx_shader.sh>
#include "xboot.sh"

SAMPLER2D(s_image, 0);

vec4 tap(vec2 p)
{
	ivec2 size = ivec2(u_api.zw);
	ivec2 q = ivec2(p);
	if (u_mode.w != 0.0 && (q.x < 0 || q.y < 0 || q.x >= size.x || q.y >= size.y)) return u_k[2];
	return texelFetch(s_image, clamp(q, ivec2(0, 0), size - ivec2(1, 1)), 0);
}

void main()
{
	vec2 p = v_t0.xy / v_t0.w * u_api.zw - 0.5;
	vec2 i = floor(p);
	vec2 f = p - i;
	vec4 t0 = mix(mix(tap(i), tap(i + vec2(1.0, 0.0)), f.x), mix(tap(i + vec2(0.0, 1.0)), tap(i + vec2(1.0, 1.0)), f.x), f.y);
	vec4 c = vec4(1.0, 1.0, 1.0, u_k[3].a);
	if (u_mode.x != 0.0) c.rgb = c.rgb * t0.rgb;
	if (u_mode.y != 0.0) c.rgb = c.rgb * u_k[3].rgb;
	if (u_mode.z != 0.0) c.a = c.a * t0.a;
	if (alpha_fails(c.a)) discard;
	gl_FragColor = c;
}
