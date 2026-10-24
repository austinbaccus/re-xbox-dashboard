$input a_position, a_texcoord0
$output v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 3.7: logo D, its colour from the vertex.

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec3 a = u_c[5].xyz + (u_c[4].xyz - u_c[5].xyz) * a_texcoord0.y;
	vec3 b = u_c[7].xyz + (u_c[6].xyz - u_c[7].xyz) * a_texcoord0.y;
	gl_Position = place(xmul(vec4(a_position, 1.0), 0));
	v_d0 = vec4(sat3(a * u_c[8].xyz + b - b * u_c[8].xyz), 1.0);
	v_t0 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t1 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t2 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t3 = vec4(0.0, 0.0, 0.0, 1.0);
}
