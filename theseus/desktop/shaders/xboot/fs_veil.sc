$input v_d0, v_t0, v_t1, v_t2, v_t3

// The block of the veil: target * (noise * K0) + K1.

#include <bgfx_shader.sh>
#include "xboot.sh"

SAMPLER2D(s_aimed, 0);
SAMPLER2D(s_noise1, 1);
SAMPLER2D(s_noise2, 2);
SAMPLER2D(s_noise3, 3);

void main()
{
	vec3 t0 = texture2DLod(s_aimed, v_t0.xy / v_t0.w, 0.0).rgb;
	float n = min(texture2DLod(s_noise1, v_t1.xy / v_t1.w, 0.0).r + texture2DLod(s_noise2, v_t2.xy / v_t2.w, 0.0).r, 1.0);
	n = min(n + texture2DLod(s_noise3, v_t3.xy / v_t3.w, 0.0).r, 1.0);
	if (alpha_fails(1.0)) discard;
	gl_FragColor = vec4(st3(st3(t0 * (n * u_k[0].rgb)) + u_k[1].rgb), 1.0);
}
