$input a_position, a_texcoord0
$output v_d0, v_t0, v_t1, v_t2, v_t3

// Fixed function (GPU.md 2.1): the rows of world * view * projection stand
// in u_c[0..3].

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	gl_Position = place(xmul(vec4(a_position, 1.0), 0));
	v_d0 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t0 = vec4(a_texcoord0, 0.0, 1.0);
	v_t1 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t2 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t3 = vec4(0.0, 0.0, 0.0, 1.0);
}
