$input a_position, a_texcoord0
$output v_d0, v_t0, v_t1, v_t2, v_t3

// D5 (GPU.md 7): a quad over the picture's place in the window.

#include <bgfx_shader.sh>

void main()
{
	gl_Position = vec4(a_position, 1.0);
	v_d0 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t0 = vec4(a_texcoord0, 0.0, 1.0);
	v_t1 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t2 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t3 = vec4(0.0, 0.0, 0.0, 1.0);
}
