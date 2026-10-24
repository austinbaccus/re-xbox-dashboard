$input v_d0, v_t0, v_t1, v_t2, v_t3

// D5 (GPU.md 7): pixel (x, y) of the picture is the mean of pixels (2x, y)
// and (2x + 1, y) of the drawing surface. u_api.zw is the picture's size.

#include <bgfx_shader.sh>
#include "xboot.sh"

SAMPLER2D(s_screen, 0);

void main()
{
	ivec2 size = ivec2(u_api.zw);
	ivec2 xy = clamp(ivec2(floor(v_t0.xy * u_api.zw)), ivec2(0, 0), size - ivec2(1, 1));
	ivec2 at = ivec2(xy.x * 2, xy.y);
	gl_FragColor = (texelFetch(s_screen, at, 0) + texelFetch(s_screen, at + ivec2(1, 0), 0)) * 0.5;
}
