$input v_d0, v_t0, v_t1, v_t2, v_t3

// The block "Depth for the veil" (GPU.md 4.3).

#include <bgfx_shader.sh>
#include "xboot.sh"

void main()
{
	vec3 e = ex3(v_t0.rgb);
	float m = 1.0 - clamp(dot(e, e), 0.0, 1.0);
	if (alpha_fails(1.0)) discard;
	gl_FragColor = vec4(max(st3(v_d0.rgb * (m * m)), vec3_splat(0.0)), 1.0);
}
