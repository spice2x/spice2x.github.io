#pragma once

#include <memory>
#include <optional>

#include <d3d9.h>

namespace d3d9_readback {

    struct SurfaceReleaser {
        void operator()(IDirect3DSurface9 *surface) const {
            surface->Release();
        }
    };

    using SurfacePtr = std::unique_ptr<IDirect3DSurface9, SurfaceReleaser>;

    // system memory copy of a back buffer; locking it neither stalls the GPU nor reads over PCIe
    struct BackbufferCopy {
        int screen {};
        D3DSURFACE_DESC desc {};
        SurfacePtr surface;
        bool pooled = false;

        BackbufferCopy() = default;
        BackbufferCopy(BackbufferCopy &&) noexcept = default;
        BackbufferCopy &operator=(BackbufferCopy &&) noexcept = default;
        BackbufferCopy(const BackbufferCopy &) = delete;
        BackbufferCopy &operator=(const BackbufferCopy &) = delete;
        ~BackbufferCopy();
    };

    // pooled copies reuse surfaces across calls and return them once the copy is destroyed,
    // so the caller must keep it alive for as long as the pixels are being read
    std::optional<BackbufferCopy> acquire_backbuffer_copy(
            IDirect3DDevice9 *device,
            IDirect3DSwapChain9 *swap_chain,
            int screen,
            bool pooled);
}
