$input a_position
$output v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 3.5: a drop of the blob.

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec4 q = vec4(u_c[12].xyz * a_position + dot(a_position, u_c[11].xyz) * u_c[13].xyz + u_c[10].xyz, u_c[8].y);
	vec3 eye = u_c[9].xyz - q.xyz;
	gl_Position = place(xmul(q, 4));
	v_d0 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t0 = vec4(a_position, 1.0);
	v_t1 = vec4(rsq(dot(eye, eye)) * eye, u_c[8].y);
	v_t2 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t3 = vec4(0.0, 0.0, 0.0, 1.0);
}
