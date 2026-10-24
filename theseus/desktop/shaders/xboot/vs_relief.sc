$input a_position, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5
$output v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 3.1 and 3.2, a part of the chamber. v_t3 is the place in the light
// view, v_d0 the attenuation, v_t1 and v_t2 light and half vector along the
// part's axes a_texcoord2, 3 and 4.

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec4 p = vec4(a_position, 1.0);
	gl_Position = place(xmul(p, 0));
	v_t3 = vec4(dot(p, u_c[8]), dot(p, u_c[9]), dot(p, u_c[10]), max(dot(p, u_c[11]), 0.0));
	vec3 q = a_position * u_c[6].xyz;
	vec3 l = u_c[4].xyz - q;
	vec3 e = u_c[5].xyz - q;
	float l2 = dot(l, l);
	float rl = rsq(l2);
	vec3 L = rl * l;
	vec3 H = rsq(dot(e, e)) * e + L;
	v_d0 = vec4(sat3(vec3_splat(1.0 / dot(vec3(1.0, l2 * rl, l2), u_c[7].xyz))), 1.0);
	v_t1 = vec4(dot(L, a_texcoord2), dot(L, a_texcoord3), dot(L, a_texcoord4), 1.0);
	v_t2 = vec4(dot(H, a_texcoord2), dot(H, a_texcoord3), dot(H, a_texcoord4), 1.0);
	v_t0 = vec4(a_texcoord5, 0.0, 1.0);
}
