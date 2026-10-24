$input a_position
$output v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 3.3: the disc around the picture of the origin, darker where the
// chamber is near (Effects.md 3).

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec4 r = xmul(vec4(a_position, 1.0), 0);
	gl_Position = place(r);
	v_d0 = vec4(sat3(max(u_c[16].xyz * (vec3_splat(r.z) + u_c[17].xyz), vec3_splat(0.0))), 1.0);
	v_t0 = vec4(r.xyz * (1.0 / r.w) * u_c[18].xyz + u_c[19].xyz, 1.0);
	v_t1 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t2 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t3 = vec4(0.0, 0.0, 0.0, 1.0);
}
