$input a_position, a_texcoord0, a_texcoord1
$output v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 3.6: the veil, the target of U2/U3 and three layers of noise.

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec4 s = vec4(a_texcoord1, 0.0, 1.0);
	gl_Position = place(vec4(a_position, 1.0));
	v_d0 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t0 = vec4(a_texcoord0, 0.0, 1.0);
	v_t1 = s * u_c[1] + u_c[0];
	v_t2 = s * u_c[3] + u_c[2];
	v_t3 = s * u_c[5] + u_c[4];
}
