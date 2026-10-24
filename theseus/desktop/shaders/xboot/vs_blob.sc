$input a_position, a_texcoord6
$output v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 3.4: the blob's mesh, a_texcoord6 its deformation (summed
// direction and height, Blob.md 6.4).

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec3 dir = a_position * u_c[11].xyz;
	vec4 q = vec4(a_position * u_c[10].xyz + u_c[12].xyz + rsq(dot(dir, dir)) * dir * a_texcoord6.w,
	              u_c[10].w + u_c[12].w + u_c[8].x * u_c[8].y);
	vec3 eye = u_c[9].xyz - q.xyz;
	vec3 n = a_texcoord6.xyz * u_c[11].xyz;
	gl_Position = place(xmul(q, 4));
	v_d0 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t0 = vec4(rsq(dot(n, n)) * n, u_c[8].y);
	v_t1 = vec4(rsq(dot(eye, eye)) * eye, u_c[8].y);
	v_t2 = vec4(0.0, 0.0, 0.0, 1.0);
	v_t3 = vec4(0.0, 0.0, 0.0, 1.0);
}
