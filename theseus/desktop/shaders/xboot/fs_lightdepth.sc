// The light views' depth offset, SET_POLYGON_OFFSET_BIAS 10 and
// _SCALE_FACTOR 4 (GPU.md 6.2). bgfx has no depth bias state, so the
// fragment applies it: units of the 16-bit depth buffer plus the slope.

#include <bgfx_shader.sh>

void main()
{
	float z = gl_FragCoord.z;
	float slope = max(abs(dFdx(z)), abs(dFdy(z)));
	gl_FragDepth = clamp(z + 10.0 / 65536.0 + 4.0 * slope, 0.0, 1.0);
}
