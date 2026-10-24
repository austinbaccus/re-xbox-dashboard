$input v_d0, v_t0, v_t1, v_t2, v_t3

// The block of the drops: the blob's rim, its alpha faded by u_k[2].a.

#include <bgfx_shader.sh>
#include "xboot.sh"

SAMPLERCUBE(s_cube, 0);

void main()
{
	float c = clamp(dot(ex3(textureCubeLod(s_cube, v_t0.xyz, 0.0).rgb),
	                    ex3(textureCubeLod(s_cube, v_t1.xyz, 0.0).rgb)), -1.0, 1.0);
	float r = (1.0 - clamp(c, 0.0, 1.0)) * (1.0 - clamp(c, 0.0, 1.0));
	gl_FragColor = vec4(st3(st3((1.0 - r) * u_k[0].rgb) + u_k[1].rgb),
	                    clamp((1.0 - r) * u_k[0].a, -1.0, 1.0) * u_k[2].a);
}
