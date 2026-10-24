// xboot.sh: shared part of the boot animation's shaders, a port of the
// WGSL in re-xbox-boot's hosts/web/src/shaders.js (the vertex programs and
// combiners of its docs/decomp/GPU.md 3 to 5). Only the picture of the
// original is kept; the web workbench's display modes are left out.
//
// u_c[k] is vertex constant k of the core, c[96 + k] of GPU.md. u_k[0..2]
// are the colours of 0x800410B0 packed to bytes (GPU.md 4.2), u_k[3] the
// factor colour of 0x8003CED0.
//
//   u_bias  x pixel offset 0.53125, y 1 = no snap, zw half the target size
//   u_misc  x alpha ref, z alpha test on, w shadow on
//   u_mode  the fixed function's stages: x texture colour, y factor colour,
//           z texture alpha, w border colour instead of the clamped edge
//   u_api   x -1 when a render target is stored bottom up (OpenGL), y 1 for
//           clip z in [-1, 1], zw the size of the texture on stage 0

uniform vec4 u_c[20];
uniform vec4 u_k[4];
uniform vec4 u_bias;
uniform vec4 u_misc;
uniform vec4 u_mode;
uniform vec4 u_api;

// From the D3D-style clip space the core computes in to the backend's.
vec4 backend(vec4 p)
{
	p.y = p.y * u_api.x;
	if (u_api.y > 0.5) p.z = 2.0 * p.z - p.w;
	return p;
}

// Every position lies 0.53125 pixels right and down, and the rasteriser
// takes it in sixteenths of a pixel, cut off towards zero (GPU.md 3).
vec4 place(vec4 p)
{
	if (abs(p.w) < 1e-12) return backend(p);
	vec2 hs = u_bias.zw;
	vec2 screen = p.xy / p.w * vec2(hs.x, -hs.y) + hs + u_bias.x;
	vec2 cut = u_bias.y != 0.0 ? screen : trunc(screen * 16.0) / 16.0;
	p.xy = (cut - hs) / vec2(hs.x, -hs.y) * p.w;
	return backend(p);
}

vec4 xmul(vec4 v, int k)
{
	return vec4(dot(v, u_c[k]), dot(v, u_c[k + 1]), dot(v, u_c[k + 2]), dot(v, u_c[k + 3]));
}

float rsq(float x) { return inversesqrt(abs(x)); }
vec3 sat3(vec3 x) { return clamp(x, vec3_splat(0.0), vec3_splat(1.0)); }
vec3 st3(vec3 x)  { return clamp(x, vec3_splat(-1.0), vec3_splat(1.0)); }
vec3 ex3(vec3 x)  { return 2.0 * max(x, vec3_splat(0.0)) - 1.0; }

// SET_ALPHA_FUNC 0x206: kept when round(a * 255) >= SET_ALPHA_REF
bool alpha_fails(float a)
{
	return u_misc.z != 0.0 && floor(a * 255.0 + 0.5) < u_misc.x;
}
