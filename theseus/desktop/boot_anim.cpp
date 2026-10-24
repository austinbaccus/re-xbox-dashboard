// boot_anim.cpp: cold-boot animation. Runs the reverse engineered boot
// animation of the original Xbox (third-party/xboot, re-xbox-boot) live:
// its portable C core computes every frame from the data pack, and this
// file draws what the core lists through bgfx and plays the sound the core
// synthesizes. When the animation ends (t = 8 s) or the user skips it,
// everything is torn down and the dashboard starts in the same window.
//
// This is a port of the boot repo's WebGPU renderer (hosts/web/src/
// renderer.js, docs/web.md 4) without its workbench. The shaders are in
// shaders/xboot/, ported from hosts/web/src/shaders.js. Section numbers
// below (GPU.md, FrameOrder.md, ...) refer to the boot repo's docs/decomp/.
//
// The boot animation (c) Microsoft / Pipeworks Software 2001.

#include "display.h"
#include "boot_anim.h"

#include <SDL.h>
#include <bgfx/bgfx.h>

#include "xboot.h"
#include "xboot_pak.h"   // generated from third-party/xboot/data by build/embed_xboot_pak.py

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

bgfx::ShaderHandle theseus_bgfx_load_shader(const char* name);

namespace {

// Sizes of the original's targets (Textures.md 1, Logo.md 6).
const uint16_t kLightSize = 512, kFxW = 1024, kFxH = 512, kLogoSize = 1024;
const float kPixelBias = 0.53125f;    // SET_VIEWPORT_OFFSET (GPU.md 3)
const int kConstants = 20;
// bgfx views of the boot animation. The dashboard has not set up any yet,
// and the ones used here are reset on the way out.
const bgfx::ViewId kViewBase = 100, kViewMax = 240;

// The programs of GPU.md 3 by their address in the ROM, plus the fixed
// function's two and the present.
enum Kind { K_SMOOTH, K_RELIEF, K_LIGHTDEPTH, K_DEPTH, K_BLOB, K_DROP, K_VEIL, K_LOGOD,
            K_FIXED, K_PLAIN, K_PRESENT, K_COUNT };
const char* const kKindName[K_COUNT] = { "smooth", "relief", "lightdepth", "depth", "blob", "drop",
                                         "veil", "logod", "fixed", "plain", "present" };

Kind KindOfProgram(uint32_t program)
{
	switch (program) {
		case 0x80065B60: return K_SMOOTH;
		case 0x80065998: return K_RELIEF;
		case 0x80065D28: return K_DEPTH;
		case 0x800662E8: return K_BLOB;
		case 0x80066470: return K_DROP;
		case 0x80066690: return K_VEIL;
		case 0x80066868: return K_LOGOD;
		default:         return K_COUNT;
	}
}

// GPU.md 5: what the library's combiners take, by kind of draw. The logo's
// meshes A, B, C and its picture take the texture alone, with the border
// colour of GPU.md 6.1.
enum { ST_TEXTURE = 1, ST_FACTOR = 2, ST_TEXTURE_ALPHA = 4, ST_BORDER = 8 };
unsigned StagesOf(uint16_t what)
{
	switch (what) {
		case XB_DRAW_BLOB_GLOW: return ST_TEXTURE | ST_FACTOR;
		case XB_DRAW_GLOW:      return ST_TEXTURE | ST_FACTOR | ST_TEXTURE_ALPHA;
		case XB_DRAW_LOGO_Q1:
		case XB_DRAW_LOGO_Q2:   return ST_TEXTURE | ST_TEXTURE_ALPHA | ST_BORDER;
		default:                return ST_TEXTURE | ST_BORDER;
	}
}

const uint32_t kClampLinear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP | BGFX_SAMPLER_MIP_POINT;
const uint32_t kRepeatLinear = BGFX_SAMPLER_MIP_POINT;
const uint32_t kPointClamp = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

// NV2A comparison 0x200 + n (GPU.md 6)
const uint64_t kDepthTest[8] = {
	BGFX_STATE_DEPTH_TEST_NEVER, BGFX_STATE_DEPTH_TEST_LESS, BGFX_STATE_DEPTH_TEST_EQUAL,
	BGFX_STATE_DEPTH_TEST_LEQUAL, BGFX_STATE_DEPTH_TEST_GREATER, BGFX_STATE_DEPTH_TEST_NOTEQUAL,
	BGFX_STATE_DEPTH_TEST_GEQUAL, BGFX_STATE_DEPTH_TEST_ALWAYS };

uint64_t BlendFactor(uint16_t v)
{
	switch (v) {
		case 0:     return BGFX_STATE_BLEND_ZERO;
		case 1:     return BGFX_STATE_BLEND_ONE;
		case 0x302: return BGFX_STATE_BLEND_SRC_ALPHA;
		case 0x303: return BGFX_STATE_BLEND_INV_SRC_ALPHA;
		default:
			fprintf(stderr, "[boot_anim] blend factor 0x%x\n", v);
			return BGFX_STATE_BLEND_ONE;
	}
}

// 0x800410B0 packs a colour to bytes: clamped, trunc(f32(x * 255 + 0.5)) (GPU.md 4.2)
float Packed(float x)
{
	float c = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
	volatile float f = c * 255.f + 0.5f;
	return (float)(int)f / 255.f;
}

void* AlignedAlloc(size_t size, void** raw)
{
	*raw = std::calloc(1, size + XB_ALIGN);
	if (!*raw) return nullptr;
	return (void*)(((uintptr_t)*raw + XB_ALIGN - 1) & ~(uintptr_t)(XB_ALIGN - 1));
}

// Where texel (x, y) of a w x h texture lies in the core's layout
// (Textures.md 3): from bit 0 up one bit of x while x has bits left, then
// one of y while y has.
uint32_t Swizzled(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t i = 0, bit = 0;
	for (uint32_t k = 0; (1u << k) < w || (1u << k) < h; k++) {
		if ((1u << k) < w) i |= ((x >> k) & 1u) << bit++;
		if ((1u << k) < h) i |= ((y >> k) & 1u) << bit++;
	}
	return i;
}

// The indices of a draw as a triangle list: bgfx has no fans.
void Triangles(uint8_t prim, const std::vector<uint16_t>& idx, std::vector<uint16_t>& out)
{
	size_t n = idx.size();
	if (prim == 5) {
		out.assign(idx.begin(), idx.begin() + (n - n % 3));
		return;
	}
	for (size_t i = 2; i < n; i++) {
		if (prim == 7)      { out.push_back(idx[0]);     out.push_back(idx[i - 1]); out.push_back(idx[i]); }
		else if (i & 1)     { out.push_back(idx[i - 1]); out.push_back(idx[i - 2]); out.push_back(idx[i]); }
		else                { out.push_back(idx[i - 2]); out.push_back(idx[i - 1]); out.push_back(idx[i]); }
	}
}

void Mul4(const double* a, const double* b, double* m)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			double s = 0;
			for (int k = 0; k < 4; k++) s += a[4 * r + k] * b[4 * k + c];
			m[4 * r + c] = s;
		}
}

struct Mesh {
	bgfx::VertexBufferHandle pos  = BGFX_INVALID_HANDLE;
	bgfx::VertexBufferHandle attr = BGFX_INVALID_HANDLE;   // chamber: axes per vertex
	uint32_t floats = 3;
	std::vector<uint16_t> idx;
};

struct Surface {
	bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
	uint16_t w = 0, h = 0;
	bool owned = true;
};

struct Uniforms {
	float c[kConstants][4];
	float k[4][4];
	float bias[4], misc[4], mode[4], api[4];
};

// The sound runs on a core of its own, so that the audio thread never
// touches the state the picture advances.
struct Sound {
	SDL_AudioDeviceID dev = 0;
	void* stateRaw = nullptr;
	void* audioRaw = nullptr;
	xb_state* state = nullptr;
};
Sound g_sound;

void SDLCALL SoundCallback(void* user, Uint8* stream, int len)
{
	xb_state* s = (xb_state*)user;
	xb_audio(s, (int16_t*)stream, (uint32_t)(len / 4));
}

void SoundClose()
{
	if (g_sound.dev) SDL_CloseAudioDevice(g_sound.dev);
	g_sound.dev = 0;
	std::free(g_sound.stateRaw);
	std::free(g_sound.audioRaw);
	g_sound = Sound();
}

bool SoundOpen(const xb_config& cfg)
{
	g_sound.state = (xb_state*)AlignedAlloc(xb_state_size(), &g_sound.stateRaw);
	void* mem = AlignedAlloc(xb_audio_size(), &g_sound.audioRaw);
	if (!g_sound.state || !mem ||
	    xb_init(g_sound.state, kXbootPak, kXbootPakSize, &cfg) != XB_OK ||
	    xb_audio_init(g_sound.state, mem, xb_audio_size()) != XB_OK) {
		SoundClose();
		return false;
	}
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq     = XB_AUDIO_RATE;
	want.format   = AUDIO_S16SYS;
	want.channels = 2;
	want.samples  = 1024;
	want.callback = SoundCallback;
	want.userdata = g_sound.state;
	g_sound.dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (!g_sound.dev) {
		fprintf(stderr, "[boot_anim] no sound: %s\n", SDL_GetError());
		SoundClose();
		return false;
	}
	return true;
}

class BootRenderer {
public:
	bool Init(const xb_config& cfg);
	void Shutdown();
	void ClearLogo();
	void Frame(int winW, int winH);

	xb_state* state = nullptr;

private:
	void Resize(uint16_t picW, uint16_t picH);
	Mesh* MeshOf(const xb_draw& d);
	bool List(const xb_draw& d, const Mesh& m, bgfx::IndexBufferHandle& ib, uint32_t& count);
	bgfx::FrameBufferHandle FrameBufferOf(uint8_t color, uint8_t zeta);
	bgfx::ViewId OpenView(uint8_t color, uint8_t zeta, bool clearDepth, bool clearColour, uint32_t rgba);
	void Submit(bgfx::ViewId view, const xb_draw& d, const float* params);
	void SetTexture(uint8_t stage, int sampler, uint8_t surf, uint32_t flags, bool cube);

	void* stateRaw_ = nullptr;
	const bgfx::Caps* caps_ = nullptr;
	bool flip_ = false;

	bgfx::VertexLayout lP3_, lP5_, lP7_, lA9_, lA11_, lDef_;
	std::map<int, Mesh> meshes_;   // XB_MESH_* * 2 + detail
	bgfx::DynamicVertexBufferHandle deformed_ = BGFX_INVALID_HANDLE;
	std::vector<float> deformedData_;
	std::map<uint64_t, std::pair<bgfx::IndexBufferHandle, uint32_t>> lists_;

	Surface surf_[XB_SURFACES];
	bgfx::TextureHandle dummy2d_ = BGFX_INVALID_HANDLE, dummyCube_ = BGFX_INVALID_HANDLE;
	std::map<int, bgfx::FrameBufferHandle> fbs_;
	uint16_t picW_ = 0, picH_ = 0;

	bgfx::ProgramHandle prog_[K_COUNT];
	bgfx::UniformHandle uC_, uK_, uBias_, uMisc_, uMode_, uApi_;
	// sampler uniforms by the name the shaders give them
	enum { S_CUBE, S_LIGHT, S_RELIEF, S_AIMED, S_NOISE1, S_NOISE2, S_NOISE3, S_IMAGE, S_SCREEN, S_COUNT };
	bgfx::UniformHandle uTex_[S_COUNT];

	std::vector<xb_draw> draws_;
	std::vector<xb_real> params_;
	bgfx::ViewId nextView_ = kViewBase;
	bgfx::ViewId usedViews_ = kViewBase;
	unsigned nanSkipped_ = 0;
};

bool BootRenderer::Init(const xb_config& cfg)
{
	for (auto& p : prog_) p = BGFX_INVALID_HANDLE;
	uC_ = uK_ = uBias_ = uMisc_ = uMode_ = uApi_ = BGFX_INVALID_HANDLE;
	for (auto& u : uTex_) u = BGFX_INVALID_HANDLE;

	state = (xb_state*)AlignedAlloc(xb_state_size(), &stateRaw_);
	if (!state) return false;
	int rc = xb_init(state, kXbootPak, kXbootPakSize, &cfg);
	if (rc != XB_OK) {
		fprintf(stderr, "[boot_anim] xb_init: %d\n", rc);
		return false;
	}

	caps_ = bgfx::getCaps();
	flip_ = caps_->originBottomLeft;

	for (int k = 0; k < K_COUNT; k++) {
		char vs[64], fs[64];
		snprintf(vs, sizeof(vs), "xboot_vs_%s", kKindName[k]);
		snprintf(fs, sizeof(fs), "xboot_fs_%s", kKindName[k]);
		bgfx::ShaderHandle v = theseus_bgfx_load_shader(vs);
		bgfx::ShaderHandle f = theseus_bgfx_load_shader(fs);
		if (!bgfx::isValid(v) || !bgfx::isValid(f)) {
			if (bgfx::isValid(v)) bgfx::destroy(v);
			if (bgfx::isValid(f)) bgfx::destroy(f);
			fprintf(stderr, "[boot_anim] shaders for %s missing\n", kKindName[k]);
			return false;
		}
		prog_[k] = bgfx::createProgram(v, f, true);
		if (!bgfx::isValid(prog_[k])) return false;
	}
	uC_    = bgfx::createUniform("u_c",    bgfx::UniformType::Vec4, kConstants);
	uK_    = bgfx::createUniform("u_k",    bgfx::UniformType::Vec4, 4);
	uBias_ = bgfx::createUniform("u_bias", bgfx::UniformType::Vec4);
	uMisc_ = bgfx::createUniform("u_misc", bgfx::UniformType::Vec4);
	uMode_ = bgfx::createUniform("u_mode", bgfx::UniformType::Vec4);
	uApi_  = bgfx::createUniform("u_api",  bgfx::UniformType::Vec4);
	static const char* const kSamplers[S_COUNT] = { "s_cube", "s_light", "s_relief", "s_aimed", "s_noise1",
	                                                "s_noise2", "s_noise3", "s_image", "s_screen" };
	for (int i = 0; i < S_COUNT; i++) uTex_[i] = bgfx::createUniform(kSamplers[i], bgfx::UniformType::Sampler);

	lP3_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
	lP5_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
	            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float).end();
	lP7_.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
	            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
	            .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float).end();
	lA9_.begin().add(bgfx::Attrib::TexCoord2, 3, bgfx::AttribType::Float)
	            .add(bgfx::Attrib::TexCoord3, 3, bgfx::AttribType::Float)
	            .add(bgfx::Attrib::TexCoord4, 3, bgfx::AttribType::Float).end();
	lA11_.begin().add(bgfx::Attrib::TexCoord5, 2, bgfx::AttribType::Float)
	             .add(bgfx::Attrib::TexCoord2, 3, bgfx::AttribType::Float)
	             .add(bgfx::Attrib::TexCoord3, 3, bgfx::AttribType::Float)
	             .add(bgfx::Attrib::TexCoord4, 3, bgfx::AttribType::Float).end();
	lDef_.begin().add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float).end();

	auto layoutOf = [&](uint32_t floats) -> const bgfx::VertexLayout& {
		return floats == 7 ? lP7_ : floats == 5 ? lP5_ : lP3_;
	};
	auto addMesh = [&](int key, std::vector<float>& v, uint32_t floats, std::vector<uint16_t>&& idx) {
		Mesh m;
		m.floats = floats;
		m.idx = std::move(idx);
		m.pos = bgfx::createVertexBuffer(bgfx::copy(v.data(), (uint32_t)(v.size() * sizeof(float))), layoutOf(floats));
		meshes_[key] = std::move(m);
		return &meshes_[key];
	};

	// Meshes, loaded once (docs/web.md 4): the chamber in both detail levels
	// with its axes per vertex, blob, drops, the seven of the logo, the two
	// quads of the veil.
	for (uint32_t g = 0; g < XB_CHAMBER_GROUPS; g++)
		for (uint32_t detail = 0; detail < 2; detail++) {
			xb_mesh_info mi;
			if (xb_chamber_mesh_info(state, g, detail, &mi) != XB_OK) return false;
			std::vector<float> pos(3 * mi.vertices), attr(mi.attr_floats * mi.vertices);
			std::vector<uint16_t> idx(mi.indices);
			std::vector<xb_form> forms(mi.forms);
			if (xb_chamber_mesh(state, g, detail, pos.data(), attr.data(), idx.data(), forms.data()) != XB_OK)
				return false;
			Mesh* m = addMesh((XB_MESH_CHAMBER + g) * 2 + detail, pos, 3, std::move(idx));
			m->attr = bgfx::createVertexBuffer(bgfx::copy(attr.data(), (uint32_t)(attr.size() * sizeof(float))),
			                                   mi.attr_floats == 11 ? lA11_ : lA9_);
		}
	const uint32_t blobN[2][2] = { { XB_BLOB_VERTICES, XB_BLOB_INDICES }, { XB_BLOB_SMALL_VERTICES, XB_BLOB_SMALL_INDICES } };
	for (uint32_t small = 0; small < 2; small++) {
		std::vector<float> unit(3 * blobN[small][0]);
		std::vector<uint16_t> idx(blobN[small][1]);
		if (xb_blob_mesh(state, small, unit.data(), idx.data()) != XB_OK) return false;
		addMesh((small ? XB_MESH_DROP : XB_MESH_BLOB) * 2, unit, 3, std::move(idx));
	}
	for (uint32_t i = 0; i < XB_LOGO_MESHES; i++) {
		xb_logo_mesh_desc ld;
		if (xb_logo_mesh_info(state, i, &ld) != XB_OK) return false;
		std::vector<float> v(ld.floats * ld.vertices);
		std::vector<uint16_t> idx(ld.indices);
		if (xb_logo_mesh(state, i, v.data(), idx.data()) != XB_OK) return false;
		addMesh((XB_MESH_LOGO + i) * 2, v, ld.floats, std::move(idx));
	}
	{
		std::vector<float> veil(4 * 7), quad(4 * 3);
		xb_fx_quads((xb_real(*)[7])veil.data(), (xb_real(*)[3])quad.data());
		addMesh(XB_MESH_VEIL * 2, veil, 7, {});
		addMesh(XB_MESH_FX_QUAD * 2, quad, 3, {});
	}
	deformedData_.resize(4 * XB_BLOB_VERTICES);
	deformed_ = bgfx::createDynamicVertexBuffer(XB_BLOB_VERTICES, lDef_);

	// The eleven textures of the set-up, level 0 only: every stage of the
	// original reads with SET_TEXTURE_FILTER 0x02022000, which stays on
	// level 0 (GPU.md 6). Put back from the core's layout into rows: R G B A
	// for the formats 6 and 7, one byte for 0x19.
	for (uint32_t t = 0; t < XB_TEXTURES; t++) {
		xb_texture_desc td;
		if (xb_texture_info(state, t, &td) != XB_OK) return false;
		std::vector<uint8_t> raw(td.bytes);
		if (xb_texture(state, t, raw.data(), raw.size()) != XB_OK) return false;
		const uint32_t w = td.width, h = td.height, n = td.texel_bytes;
		const uint32_t faceBytes = td.bytes / td.faces;
		const bgfx::Memory* mem = bgfx::alloc(w * h * (n == 1 ? 1 : 4) * td.faces);
		for (uint32_t face = 0; face < td.faces; face++) {
			uint8_t* out = mem->data + face * w * h * (n == 1 ? 1 : 4);
			const uint8_t* from = raw.data() + face * faceBytes;
			for (uint32_t y = 0; y < h; y++)
				for (uint32_t x = 0; x < w; x++) {
					const uint8_t* s = from + Swizzled(x, y, w, h) * n;
					uint32_t i = y * w + x;
					if (n == 1) { out[i] = s[0]; continue; }
					out[4 * i + 0] = s[2];
					out[4 * i + 1] = s[1];
					out[4 * i + 2] = s[0];
					out[4 * i + 3] = td.format == 7 ? 255 : s[3];
				}
		}
		Surface& sf = surf_[XB_SURF_TEX + t];
		sf.w = (uint16_t)w;
		sf.h = (uint16_t)h;
		if (td.faces == 6)
			sf.tex = bgfx::createTextureCube((uint16_t)w, false, 1, bgfx::TextureFormat::RGBA8, kClampLinear, mem);
		else
			sf.tex = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1,
			                               n == 1 ? bgfx::TextureFormat::R8 : bgfx::TextureFormat::RGBA8, 0, mem);
	}

	// Targets. Depth of the light views 16 bits as in the original, the
	// others float where the GPU can.
	auto rt = [&](int id, uint16_t w, uint16_t h, bgfx::TextureFormat::Enum fmt) {
		surf_[id].tex = bgfx::createTexture2D(w, h, false, 1, fmt, BGFX_TEXTURE_RT | kPointClamp);
		surf_[id].w = w;
		surf_[id].h = h;
	};
	const uint16_t fbDepthCaps = BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER | BGFX_CAPS_FORMAT_TEXTURE_2D;
	bgfx::TextureFormat::Enum lightFmt = (caps_->formats[bgfx::TextureFormat::D16] & fbDepthCaps) == fbDepthCaps
		? bgfx::TextureFormat::D16 : bgfx::TextureFormat::D32F;
	rt(XB_SURF_LIGHT0, kLightSize, kLightSize, lightFmt);
	rt(XB_SURF_LIGHT1, kLightSize, kLightSize, lightFmt);
	rt(XB_SURF_FX_A, kFxW, kFxH, bgfx::TextureFormat::RGBA8);
	rt(XB_SURF_FX_B, kFxW, kFxH, bgfx::TextureFormat::RGBA8);
	rt(XB_SURF_FX_DEPTH, kFxW, kFxH, bgfx::TextureFormat::D32F);
	rt(XB_SURF_LOGO_TARGET, kLogoSize, kLogoSize, bgfx::TextureFormat::RGBA8);
	rt(XB_SURF_LOGO_DEPTH, kLogoSize, kLogoSize, bgfx::TextureFormat::D32F);
	// logo_render_to_texture copies its target into the texture logo+0xD0
	// (Logo.md 6). Nothing draws into the target but that copy's source,
	// and every frame that draws it copies it, so the texture is the
	// target itself here.
	surf_[XB_SURF_LOGO] = surf_[XB_SURF_LOGO_TARGET];
	surf_[XB_SURF_LOGO].owned = false;

	const uint32_t white = 0xFFFFFFFF;
	dummy2d_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(&white, 4));
	uint32_t white6[6] = { white, white, white, white, white, white };
	dummyCube_ = bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(white6, sizeof(white6)));

	draws_.resize(XB_FRAME_DRAWS_MAX);
	params_.resize(XB_FRAME_PARAMS_MAX);
	return true;
}

void BootRenderer::Shutdown()
{
	for (auto& f : fbs_) bgfx::destroy(f.second);
	fbs_.clear();
	for (auto& s : surf_)
		if (s.owned && bgfx::isValid(s.tex)) bgfx::destroy(s.tex);
	for (auto& m : meshes_) {
		if (bgfx::isValid(m.second.pos))  bgfx::destroy(m.second.pos);
		if (bgfx::isValid(m.second.attr)) bgfx::destroy(m.second.attr);
	}
	meshes_.clear();
	for (auto& l : lists_) bgfx::destroy(l.second.first);
	lists_.clear();
	if (bgfx::isValid(deformed_)) bgfx::destroy(deformed_);
	if (bgfx::isValid(dummy2d_))   bgfx::destroy(dummy2d_);
	if (bgfx::isValid(dummyCube_)) bgfx::destroy(dummyCube_);
	for (auto& p : prog_)
		if (bgfx::isValid(p)) bgfx::destroy(p);
	for (bgfx::UniformHandle u : { uC_, uK_, uBias_, uMisc_, uMode_, uApi_ })
		if (bgfx::isValid(u)) bgfx::destroy(u);
	for (bgfx::UniformHandle u : uTex_)
		if (bgfx::isValid(u)) bgfx::destroy(u);
	for (bgfx::ViewId v = kViewBase; v < usedViews_; v++) {
		bgfx::setViewFrameBuffer(v, BGFX_INVALID_HANDLE);
		bgfx::setViewClear(v, BGFX_CLEAR_NONE);
		bgfx::setViewMode(v, bgfx::ViewMode::Default);
	}
	std::free(stateRaw_);
	stateRaw_ = nullptr;
	state = nullptr;
	if (nanSkipped_)
		fprintf(stderr, "[boot_anim] %u draws with NaN constants left out (Chamber.md 6)\n", nanSkipped_);
}

// The drawing surface for a picture of picW x picH: twice as wide, so that
// D5 is the mean of two pixels (GPU.md 7). At 640 x 480 the original's
// 1280 x 480.
void BootRenderer::Resize(uint16_t picW, uint16_t picH)
{
	if (picW == picW_ && picH == picH_) return;
	for (auto it = fbs_.begin(); it != fbs_.end();) {
		int color = it->first >> 8, zeta = it->first & 255;
		if (color == XB_SURF_SCREEN || zeta == XB_SURF_SCREEN_DEPTH) {
			bgfx::destroy(it->second);
			it = fbs_.erase(it);
		} else {
			++it;
		}
	}
	for (int id : { XB_SURF_SCREEN, XB_SURF_SCREEN_DEPTH })
		if (bgfx::isValid(surf_[id].tex)) bgfx::destroy(surf_[id].tex);
	picW_ = picW;
	picH_ = picH;
	const uint16_t w = (uint16_t)(2 * picW);
	surf_[XB_SURF_SCREEN].tex = bgfx::createTexture2D(w, picH, false, 1, bgfx::TextureFormat::RGBA8,
	                                                  BGFX_TEXTURE_RT | kPointClamp);
	surf_[XB_SURF_SCREEN_DEPTH].tex = bgfx::createTexture2D(w, picH, false, 1, bgfx::TextureFormat::D32F,
	                                                        BGFX_TEXTURE_RT | kPointClamp);
	for (int id : { XB_SURF_SCREEN, XB_SURF_SCREEN_DEPTH }) {
		surf_[id].w = w;
		surf_[id].h = picH;
		surf_[id].owned = true;
	}
}

Mesh* BootRenderer::MeshOf(const xb_draw& d)
{
	auto it = meshes_.find(d.mesh * 2 + (d.mesh < XB_MESH_BLOB ? d.detail : 0));
	return it == meshes_.end() ? nullptr : &it->second;
}

// Triangle list of a draw, cached as its own index buffer.
bool BootRenderer::List(const xb_draw& d, const Mesh& m, bgfx::IndexBufferHandle& ib, uint32_t& count)
{
	const bool indexed = (d.flags & XB_DRAW_INDEXED) != 0;
	const uint64_t key = (uint64_t)d.mesh << 56 | (uint64_t)d.detail << 55 | (uint64_t)d.prim << 52 |
	                     (uint64_t)indexed << 51 | (uint64_t)(d.first & 0xFFFFFF) << 24 | (d.count & 0xFFFFFF);
	auto it = lists_.find(key);
	if (it == lists_.end()) {
		std::vector<uint16_t> src(d.count), tri;
		for (uint32_t i = 0; i < d.count; i++) {
			if (!indexed) { src[i] = (uint16_t)(d.first + i); continue; }
			if (d.first + i >= m.idx.size()) return false;
			src[i] = m.idx[d.first + i];
		}
		Triangles(d.prim, src, tri);
		bgfx::IndexBufferHandle h = BGFX_INVALID_HANDLE;
		if (!tri.empty())
			h = bgfx::createIndexBuffer(bgfx::copy(tri.data(), (uint32_t)(tri.size() * sizeof(uint16_t))));
		it = lists_.emplace(key, std::make_pair(h, (uint32_t)tri.size())).first;
	}
	ib = it->second.first;
	count = it->second.second;
	return bgfx::isValid(ib) && count > 0;
}

bgfx::FrameBufferHandle BootRenderer::FrameBufferOf(uint8_t color, uint8_t zeta)
{
	const int key = color << 8 | zeta;
	auto it = fbs_.find(key);
	if (it != fbs_.end()) return it->second;
	bgfx::TextureHandle att[2];
	uint8_t n = 0;
	if (color != XB_SURF_NONE) att[n++] = surf_[color].tex;
	att[n++] = surf_[zeta].tex;
	bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(n, att, false);
	fbs_[key] = fb;
	return fb;
}

bgfx::ViewId BootRenderer::OpenView(uint8_t color, uint8_t zeta, bool clearDepth, bool clearColour, uint32_t rgba)
{
	if (nextView_ >= kViewMax) return kViewMax - 1;   // never seen; keeps drawing into the last one
	bgfx::ViewId v = nextView_++;
	if (nextView_ > usedViews_) usedViews_ = nextView_;
	bgfx::setViewMode(v, bgfx::ViewMode::Sequential);
	bgfx::setViewFrameBuffer(v, FrameBufferOf(color, zeta));
	bgfx::setViewRect(v, 0, 0, surf_[zeta].w, surf_[zeta].h);
	bgfx::setViewTransform(v, nullptr, nullptr);
	uint16_t flags = (clearDepth ? BGFX_CLEAR_DEPTH : 0) | (clearColour ? BGFX_CLEAR_COLOR : 0);
	bgfx::setViewClear(v, flags ? flags : BGFX_CLEAR_NONE, rgba, 1.0f, 0);
	bgfx::touch(v);
	return v;
}

void BootRenderer::SetTexture(uint8_t stage, int sampler, uint8_t surf, uint32_t flags, bool cube)
{
	bgfx::TextureHandle t = BGFX_INVALID_HANDLE;
	if (surf != XB_SURF_NONE && surf < XB_SURFACES) t = surf_[surf].tex;
	if (!bgfx::isValid(t)) t = cube ? dummyCube_ : dummy2d_;
	bgfx::setTexture(stage, uTex_[sampler], t, flags);
}

void BootRenderer::ClearLogo()
{
	nextView_ = kViewBase;
	OpenView(XB_SURF_LOGO_TARGET, XB_SURF_LOGO_DEPTH, true, true, 0x000000ff);
	bgfx::frame();
}

// One entry of the frame (renderer.js, prepare and pass).
void BootRenderer::Submit(bgfx::ViewId view, const xb_draw& d, const float* params)
{
	Mesh* mesh = nullptr;
	if (d.mesh != XB_MESH_INLINE && !(mesh = MeshOf(d))) return;

	Kind kind;
	if (d.step == XB_STEP_U1) kind = K_LIGHTDEPTH;
	else if ((kind = KindOfProgram(d.program)) == K_COUNT)
		kind = d.what == XB_DRAW_LOGO_E ? K_PLAIN : K_FIXED;
	if ((kind == K_SMOOTH || kind == K_RELIEF) && !bgfx::isValid(mesh->attr)) return;

	Uniforms u;
	std::memset(&u, 0, sizeof(u));
	if (d.flags & XB_DRAW_MATRICES) {
		// clip = v * world * view * proj with row vectors: row j of the
		// constants is column j of the product
		double w[16], v[16], p[16], wv[16], m[16];
		for (int i = 0; i < 16; i++) { w[i] = params[d.world + i]; v[i] = params[d.view + i]; p[i] = params[d.proj + i]; }
		Mul4(w, v, wv);
		Mul4(wv, p, m);
		for (int j = 0; j < 4; j++)
			for (int i = 0; i < 4; i++) u.c[j][i] = (float)m[4 * i + j];
	} else {
		for (const xb_const_run& run : d.vs)
			for (uint32_t k = 0; k < run.count && run.base + k < (uint32_t)kConstants; k++)
				std::memcpy(u.c[run.base + k], params + run.first + 4 * k, 4 * sizeof(float));
	}
	// Parts at the ROM's quaternion 238 have NaN in their matrices until
	// t = 1.17 (Chamber.md 6). In xemu they leave no depth
	// (FrameOrder.md 4); not drawn here.
	for (int i = 0; i < kConstants; i++)
		for (int j = 0; j < 4; j++)
			if (std::isnan(u.c[i][j])) { nanSkipped_++; return; }
	const uint32_t ps = 4 * (d.ps_count < 3 ? d.ps_count : 3);
	for (uint32_t i = 0; i < ps; i++) u.k[i / 4][i % 4] = Packed(params[d.ps_first + i]);
	if (kind == K_FIXED || kind == K_PLAIN) {
		uint32_t f = (d.flags & XB_DRAW_FACTOR) ? d.factor : 0xFFFFFFFFu;
		u.k[3][0] = ((f >> 16) & 255) / 255.f;
		u.k[3][1] = ((f >> 8) & 255) / 255.f;
		u.k[3][2] = (f & 255) / 255.f;
		u.k[3][3] = (f >> 24) / 255.f;
		u.k[2][3] = 1.f;   // SET_TEXTURE_BORDER_COLOR 0xFF000000 (GPU.md 2.2)
	}
	const Surface& target = surf_[d.zeta];
	u.bias[0] = kPixelBias;
	u.bias[2] = target.w / 2.f;
	u.bias[3] = target.h / 2.f;
	if (d.state.alpha_test && d.state.alpha_func != 0x206) {
		static bool warned = false;
		if (!warned) fprintf(stderr, "[boot_anim] alpha function 0x%x\n", d.state.alpha_func);
		warned = true;
	}
	u.misc[0] = (float)d.state.alpha_ref;
	u.misc[2] = d.state.alpha_test ? 1.f : 0.f;
	u.misc[3] = d.state.shadow_func == 0x204 ? 1.f : 0.f;
	const unsigned stages = StagesOf(d.what);
	u.mode[0] = (stages & ST_TEXTURE) ? 1.f : 0.f;
	u.mode[1] = (stages & ST_FACTOR) ? 1.f : 0.f;
	u.mode[2] = (stages & ST_TEXTURE_ALPHA) ? 1.f : 0.f;
	u.mode[3] = (stages & ST_BORDER) ? 1.f : 0.f;
	u.api[0] = flip_ ? -1.f : 1.f;
	u.api[1] = caps_->homogeneousDepth ? 1.f : 0.f;
	if (d.tex[0] < XB_SURFACES) {
		u.api[2] = surf_[d.tex[0]].w;
		u.api[3] = surf_[d.tex[0]].h;
	}

	// Indices and vertices
	bgfx::TransientVertexBuffer tvb;
	if (d.mesh == XB_MESH_INLINE) {
		const uint32_t n = d.vtx_count / 5;
		if (n == 0 || bgfx::getAvailTransientVertexBuffer(n, lP5_) < n) return;
		bgfx::allocTransientVertexBuffer(&tvb, n, lP5_);
		std::memcpy(tvb.data, params + d.vtx_first, n * 5 * sizeof(float));
	}
	static const Mesh kInline;
	bgfx::IndexBufferHandle ib;
	uint32_t count = 0;
	if (!List(d, mesh ? *mesh : kInline, ib, count)) return;

	// Render states in effect at the draw (GPU.md 6)
	uint64_t st = 0;
	if (kind != K_LIGHTDEPTH && d.color != XB_SURF_NONE) {
		const uint32_t cm = d.state.colour_mask;
		if (cm & 0x00010000u) st |= BGFX_STATE_WRITE_R;
		if (cm & 0x00000100u) st |= BGFX_STATE_WRITE_G;
		if (cm & 0x00000001u) st |= BGFX_STATE_WRITE_B;
		if (cm & 0x01000000u) st |= BGFX_STATE_WRITE_A;
	}
	// without the depth test nothing is compared and nothing written
	if (d.state.depth_test) {
		st |= kDepthTest[d.state.depth_func & 7];
		if (d.state.depth_write) st |= BGFX_STATE_WRITE_Z;
	}
	if (d.state.blend)
		st |= BGFX_STATE_BLEND_FUNC(BlendFactor(d.state.blend_src), BlendFactor(d.state.blend_dst));
	if (d.state.cull) {
		// front faces are clockwise (GPU.md 6.3); a target stored bottom up
		// turns every triangle over
		bool cullCw = d.state.cull_face == 0x404;
		if (flip_) cullCw = !cullCw;
		st |= cullCw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
	}

	switch (kind) {
		case K_SMOOTH:
		case K_RELIEF:
			SetTexture(0, S_CUBE, d.tex[1], kClampLinear, true);
			SetTexture(1, S_LIGHT, d.tex[3], kPointClamp, false);
			if (kind == K_RELIEF) SetTexture(2, S_RELIEF, d.tex[0], kRepeatLinear, false);
			break;
		case K_BLOB:
		case K_DROP:
			SetTexture(0, S_CUBE, d.tex[0], kClampLinear, true);
			break;
		case K_VEIL:
			SetTexture(0, S_AIMED, d.tex[0], kClampLinear, false);
			for (uint8_t s = 1; s < 4; s++) SetTexture(s, S_NOISE1 + s - 1, d.tex[s], kRepeatLinear, false);
			break;
		case K_FIXED:
			SetTexture(0, S_IMAGE, d.tex[0], kPointClamp, false);
			break;
		default:
			break;
	}

	bgfx::setUniform(uC_, u.c, kConstants);
	bgfx::setUniform(uK_, u.k, 4);
	bgfx::setUniform(uBias_, u.bias);
	bgfx::setUniform(uMisc_, u.misc);
	bgfx::setUniform(uMode_, u.mode);
	bgfx::setUniform(uApi_, u.api);
	bgfx::setState(st);
	if (d.mesh == XB_MESH_INLINE) {
		bgfx::setVertexBuffer(0, &tvb);
	} else {
		bgfx::setVertexBuffer(0, mesh->pos);
		if (kind == K_SMOOTH || kind == K_RELIEF) bgfx::setVertexBuffer(1, mesh->attr);
		if (kind == K_BLOB) bgfx::setVertexBuffer(1, deformed_);
	}
	bgfx::setIndexBuffer(ib, 0, count);
	bgfx::submit(view, prog_[kind]);
}

// Draws the frame of the core's last advance (renderer.js, draw).
void BootRenderer::Frame(int winW, int winH)
{
	// The picture in 4:3 as large as the window holds, centred.
	uint16_t picH = (uint16_t)winH, picW = (uint16_t)((winH * 4 + 1) / 3);
	if (picW > winW) { picW = (uint16_t)winW; picH = (uint16_t)(winW * 3 / 4); }
	const uint32_t maxTex = caps_->limits.maxTextureSize;
	if (2u * picW > maxTex) { picW = (uint16_t)(maxTex / 2); picH = (uint16_t)(picW * 3 / 4); }
	if (picW < 4 || picH < 3) { picW = 640; picH = 480; }
	Resize(picW, picH);

	xb_frame_out fo;
	std::memset(&fo, 0, sizeof(fo));
	fo.draws = draws_.data();
	fo.draw_cap = (uint32_t)draws_.size();
	fo.params = params_.data();
	fo.param_cap = (uint32_t)params_.size();
	int rc = xb_frame(state, &fo);
	if (rc != XB_OK) {
		fprintf(stderr, "[boot_anim] xb_frame: %d\n", rc);
		return;
	}
	if (xb_blob_deformed(state, (xb_real(*)[4])deformedData_.data()) == XB_OK)
		bgfx::update(deformed_, 0, bgfx::copy(deformedData_.data(), (uint32_t)(deformedData_.size() * sizeof(float))));

	// Consecutive draws into one target are one pass. A target is cleared
	// where the frame first draws into it: the light views to depth 1
	// (Chamber.md 8), the veil's target to 0xFFFF0000 (Effects.md 3), the
	// logo's and the surface to black (Logo.md 6, FrameOrder.md 1).
	nextView_ = kViewBase;
	bool begun[XB_SURFACES] = {};
	bool open = false, cleared = !fo.clear_depth;
	uint8_t curColor = 0, curZeta = 0;
	bgfx::ViewId view = kViewBase;
	auto openGroup = [&](uint8_t color, uint8_t zeta, bool clearDepth) {
		const bool first = !begun[zeta];
		begun[zeta] = true;
		const bool fx = color == XB_SURF_FX_A || color == XB_SURF_FX_B;
		view = OpenView(color, zeta, first || clearDepth, first && color != XB_SURF_NONE, fx ? 0xff0000ff : 0x000000ff);
		curColor = color;
		curZeta = zeta;
		open = true;
	};
	for (uint32_t i = 0; i < fo.draw_count; i++) {
		const xb_draw& d = draws_[i];
		if (d.color == XB_SURF_DISPLAY || d.what == XB_DRAW_PRESENT) continue;
		if (d.zeta >= XB_SURFACES || !bgfx::isValid(surf_[d.zeta].tex)) continue;   // the cube of the set-up
		if (d.color != XB_SURF_NONE && (d.color >= XB_SURFACES || !bgfx::isValid(surf_[d.color].tex))) continue;
		// anim_draw clears depth before the logo when the core says so
		const bool clearNow = d.color == XB_SURF_SCREEN && d.step == XB_STEP_D3 && !cleared;
		if (!open || curColor != d.color || curZeta != d.zeta || clearNow)
			openGroup(d.color, d.zeta, clearNow);
		cleared = cleared || clearNow;
		Submit(view, d, params_.data());
	}
	if (!begun[XB_SURF_SCREEN_DEPTH])
		openGroup(XB_SURF_SCREEN, XB_SURF_SCREEN_DEPTH, true);

	// D5: the picture into the window, black around it.
	bgfx::ViewId clearView = nextView_ < kViewMax ? nextView_++ : kViewMax - 1;
	bgfx::ViewId presentView = nextView_ < kViewMax ? nextView_++ : kViewMax - 1;
	if (nextView_ > usedViews_) usedViews_ = nextView_;
	bgfx::setViewFrameBuffer(clearView, BGFX_INVALID_HANDLE);
	bgfx::setViewRect(clearView, 0, 0, (uint16_t)winW, (uint16_t)winH);
	bgfx::setViewClear(clearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
	bgfx::touch(clearView);
	bgfx::setViewFrameBuffer(presentView, BGFX_INVALID_HANDLE);
	bgfx::setViewRect(presentView, (uint16_t)((winW - picW) / 2), (uint16_t)((winH - picH) / 2), picW, picH);
	bgfx::setViewClear(presentView, BGFX_CLEAR_NONE);
	bgfx::setViewTransform(presentView, nullptr, nullptr);
	if (bgfx::getAvailTransientVertexBuffer(3, lP5_) >= 3) {
		bgfx::TransientVertexBuffer tvb;
		bgfx::allocTransientVertexBuffer(&tvb, 3, lP5_);
		const float tri[15] = { -1.f, 1.f, 0.f, 0.f, 0.f,
		                         3.f, 1.f, 0.f, 2.f, 0.f,
		                        -1.f, -3.f, 0.f, 0.f, 2.f };
		std::memcpy(tvb.data, tri, sizeof(tri));
		float api[4] = { 1.f, 0.f, (float)picW, (float)picH };
		bgfx::setUniform(uApi_, api);
		bgfx::setTexture(0, uTex_[S_SCREEN], surf_[XB_SURF_SCREEN].tex, kPointClamp);
		bgfx::setVertexBuffer(0, &tvb);
		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
		bgfx::submit(presentView, prog_[K_PRESENT]);
	}
}

} // namespace

bool BootAnim_PlayAndWait(SDL_Window* win)
{
	if (!win) return false;

	xb_config cfg;
	cfg.flags = 0;
	cfg.start_tick = 0;
	BootRenderer r;
	if (!r.Init(cfg)) {
		fprintf(stderr, "[boot_anim] could not start the boot animation; skipping\n");
		r.Shutdown();
		return false;
	}
	r.ClearLogo();
	const bool sound = SoundOpen(cfg);

	bool running = true, drew = false;
	const Uint32 start = SDL_GetTicks();
	if (sound) SDL_PauseAudioDevice(g_sound.dev, 0);
	while (running) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) running = false;
			else if (e.type == SDL_KEYDOWN &&
			         (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_RETURN ||
			          e.key.keysym.sym == SDLK_SPACE))
				running = false;
			else if (e.type == SDL_CONTROLLERBUTTONDOWN &&
			         (e.cbutton.button == SDL_CONTROLLER_BUTTON_A || e.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
			          e.cbutton.button == SDL_CONTROLLER_BUTTON_START))
				running = false;
		}
		if (!running) break;

		// The core runs by the millisecond counter, not by frames (Timing.md 2).
		int rc = xb_advance(r.state, SDL_GetTicks() - start);
		if (rc < 0) {
			fprintf(stderr, "[boot_anim] xb_advance: %d\n", rc);
			break;
		}

		// Runs before the main loop exists, so it has to do its own
		// reconcile or the whole animation plays at the wrong scale.
		extern void Gfx_ReconcileDrawable();
		Gfx_ReconcileDrawable();
		int winW = 0, winH = 0;
		Plat_GetDrawableSize(&winW, &winH);
		if (winW <= 0) winW = 1;
		if (winH <= 0) winH = 1;
		r.Frame(winW, winH);
		bgfx::frame();
		drew = true;
		if (rc == XB_DONE) running = false;
	}

	r.Shutdown();
	// One more frame so the views above are reset before the dashboard draws.
	bgfx::frame();

	// The sound's tail after t = 8 is some 40 dB down and gone by 9.6 s; it
	// stops here so the dashboard's mixer gets the audio device to itself.
	if (sound) SoundClose();
	return drew;
}
