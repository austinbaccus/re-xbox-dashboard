$input v_d0, v_t0, v_t1, v_t2, v_t3

// GPU.md 4.3, the block of the parts of the chamber with relief: as
// fs_smooth, the normal from the relief texture and nh to the power 16.

#include <bgfx_shader.sh>
#include "xboot.sh"

SAMPLERCUBE(s_cube, 0);
SAMPLER2D(s_light, 1);
SAMPLER2D(s_relief, 2);

#include "xboot_shadow.sh"

void main()
{
	vec3 t1 = ex3(textureCubeLod(s_cube, v_t1.xyz, 0.0).rgb);
	vec3 t2 = ex3(textureCubeLod(s_cube, v_t2.xyz, 0.0).rgb);
	vec3 normal = ex3(texture2DLod(s_relief, v_t0.xy / v_t0.w, 0.0).rgb);
	float nl = clamp(dot(normal, t1), -1.0, 1.0);
	float s = max(clamp(dot(normal, t2), -1.0, 1.0), 0.0);
	s = s * s; s = s * s; s = s * s; s = s * s;
	vec3 lit = st3(u_k[1].rgb * max(nl, 0.0) + u_k[2].rgb * s);
	vec3 r0 = max(st3(v_d0.rgb * lit + u_k[0].rgb), vec3_splat(0.0));
	gl_FragColor = vec4(r0 * (63.0 / 255.0) + r0 * shadow(v_t3), 1.0);
}
