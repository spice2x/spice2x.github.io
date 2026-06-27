#include "d3d9_live2d.h"

// only the Live2D-capable SDVX versions are 64-bit, so the entire implementation
// is compiled out of 32-bit builds (the header supplies inline no-op stubs there).
#ifdef SPICE64

#include <cstdint>
#include <unordered_set>

#include "hooks/graphics/graphics.h"

// how the Live2D draw filtering works
// ------------------------------------
// SDVX draws its Live2D (Cubism) characters with a small, fixed set of pixel and
// vertex shaders. to skip those draws (and save GPU) we have to recognise them at
// the exact moment the game issues a draw call. the d3d9 device hooks feed three
// kinds of events into this module:
//
//   1. shader creation  (on_create_pixel_shader / on_create_vertex_shader)
//        the game compiles its shaders once at load. we can't trust the shader
//        *object pointer* to identify a shader (it's just a heap address that
//        varies per run and can be recycled), so instead we hash the shader's
//        D3D9 *bytecode* - that fingerprint is stable across runs because the
//        game ships the same shaders. if the hash matches a known Live2D shader
//        we remember that object pointer in g_live2d_shaders.
//
//   2. shader binding   (on_set_pixel_shader / on_set_vertex_shader)
//        whenever the game binds a shader we look it up in that set once and cache
//        the yes/no answer in g_cur_ps_is_live2d / g_cur_vs_is_live2d. binds happen
//        far less often than draws, so this is where the lookup cost lives.
//
//   3. draw call        (should_skip_draw, called from every Draw* hook)
//        the per-draw question "is this a Live2D draw?" is then just reading those
//        two cached bools - no hashing, no map lookups. if the skip is currently
//        active (see graphics_sdvx_live2d_should_skip) and either bound shader is
//        Live2D, the Draw* hook drops the call instead of forwarding it.
//
// everything is gated on the feature being enabled (mode != Off); when it's Off
// every entry point is a single predicted-not-taken branch. d3d9 rendering for a
// device is single-threaded, so none of this state needs locking.
// see /memories/repo/sdvx-live2d-disable.md for how the hashes were captured.

namespace {

// shader state is tracked whenever the feature might act (mode != Off) so the
// known-shader set is populated before a song starts. when Off, every entry
// point is a single cheap branch.
bool tracking_enabled() {
    return GRAPHICS_SDVX_LIVE2D_MODE != SdvxLive2dMode::Off;
}

// the set of shader objects (pixel or vertex) whose bytecode matched a known
// Live2D fingerprint. only matching shaders are stored, so this stays tiny.
std::unordered_set<void *> g_live2d_shaders;

// whether the currently-bound shaders are known Live2D shaders. cached at set
// time so the per-draw check is just two bool reads.
bool g_cur_ps_is_live2d = false;
bool g_cur_vs_is_live2d = false;

// FNV-1a 64 over a D3D9 shader token stream (ends with D3DSIO_END = 0x0000FFFF)
uint64_t bytecode_hash(const DWORD *func) {
    if (func == nullptr) {
        return 0;
    }
    const DWORD *p = func;
    const DWORD *cap = func + 65536; // safety bound
    while (p < cap && *p != 0x0000FFFF) {
        p++;
    }
    const size_t n_bytes = ((size_t)(p - func) + 1) * sizeof(DWORD);
    uint64_t h = 1469598103934665603ULL;
    const auto *bytes = reinterpret_cast<const uint8_t *>(func);
    for (size_t i = 0; i < n_bytes; i++) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// known SDVX Live2D (Cubism) shader bytecode hashes (4 pixel + 3 vertex). stable
// across runs because the game ships fixed shaders. the two sets are disjoint so
// a single shader can be classified by its own hash alone.
// see /memories/repo/sdvx-live2d-disable.md.
bool hash_is_live2d(uint64_t hash) {
    switch (hash) {
    case 0x75c89951817421a4ULL: // pixel: dominant model draw (~4.9M prims/120f in-song)
    case 0x2d7ce428c6b4775dULL: // pixel: masked model draw
    case 0x3ce00cc6111c10e7ULL: // pixel: mask generation
    case 0x8bb3a2f37150ac34ULL: // pixel: mask generation (variant)
    case 0xe9cf898c331e2a51ULL: // vertex
    case 0x94dc84e7b7c0f437ULL: // vertex
    case 0xc872937c5cc04309ULL: // vertex
        return true;
    }
    return false;
}

// classify a shader at creation time and record it if it is Live2D. erasing on a
// miss keeps the set correct if the runtime reuses a freed shader pointer.
void classify_shader(void *shader, const DWORD *func) {
    if (hash_is_live2d(bytecode_hash(func))) {
        g_live2d_shaders.insert(shader);
    } else {
        g_live2d_shaders.erase(shader);
    }
}

} // namespace

namespace d3d9_live2d {

// stage 1: fingerprint each shader as the game creates it
void on_create_vertex_shader(IDirect3DVertexShader9 *shader, const DWORD *func) {
    if (tracking_enabled() && shader != nullptr) [[unlikely]] {
        classify_shader(shader, func);
    }
}

void on_create_pixel_shader(IDirect3DPixelShader9 *shader, const DWORD *func) {
    if (tracking_enabled() && shader != nullptr) [[unlikely]] {
        classify_shader(shader, func);
    }
}

// stage 2: remember whether the just-bound shader is a Live2D one
void on_set_vertex_shader(IDirect3DVertexShader9 *shader) {
    if (tracking_enabled()) [[unlikely]] {
        g_cur_vs_is_live2d = g_live2d_shaders.count(shader) != 0;
    }
}

void on_set_pixel_shader(IDirect3DPixelShader9 *shader) {
    if (tracking_enabled()) [[unlikely]] {
        g_cur_ps_is_live2d = g_live2d_shaders.count(shader) != 0;
    }
}

// stage 3: drop the draw if the skip is active and a Live2D shader is bound
bool should_skip_draw() {
    return graphics_sdvx_live2d_should_skip() && (g_cur_ps_is_live2d || g_cur_vs_is_live2d);
}

} // namespace d3d9_live2d

#endif // SPICE64
