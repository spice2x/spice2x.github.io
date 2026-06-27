#pragma once

#include <windows.h>
#include <d3d9.h>

// SDVX Live2D draw-skip support for the D3D9 backend.
//
// SDVX renders its Live2D navigator / in-song character through a fixed set of
// shaders. when the skip is active (see graphics_sdvx_live2d_should_skip)
// the matching draw calls are dropped to save GPU. shaders are identified by a
// stable hash of their D3D9 bytecode (object pointers vary per run, the bytecode
// does not). the hashes were captured with the draw-call fingerprinting tool;
// see /memories/repo/sdvx-live2d-disable.md.
//
// every entry point is a no-op unless the feature is enabled (mode != Off), and
// d3d9 rendering for a device is single-threaded, so none of this needs locking.
namespace d3d9_live2d {

#ifdef SPICE64

    // record a shader's bytecode fingerprint at creation time
    void on_create_vertex_shader(IDirect3DVertexShader9 *shader, const DWORD *func);
    void on_create_pixel_shader(IDirect3DPixelShader9 *shader, const DWORD *func);

    // remember the currently-bound shaders
    void on_set_vertex_shader(IDirect3DVertexShader9 *shader);
    void on_set_pixel_shader(IDirect3DPixelShader9 *shader);

    // true if the current draw call should be dropped (skip active AND the bound
    // shaders identify it as SDVX Live2D)
    bool should_skip_draw();

#else // !SPICE64

    // only the Live2D-capable SDVX versions are 64-bit; on 32-bit every entry point
    // compiles away to nothing, so the d3d9 device hooks need no #ifdefs at their
    // call sites.
    inline void on_create_vertex_shader(IDirect3DVertexShader9 *, const DWORD *) {}
    inline void on_create_pixel_shader(IDirect3DPixelShader9 *, const DWORD *) {}
    inline void on_set_vertex_shader(IDirect3DVertexShader9 *) {}
    inline void on_set_pixel_shader(IDirect3DPixelShader9 *) {}
    inline bool should_skip_draw() { return false; }

#endif // SPICE64
}
