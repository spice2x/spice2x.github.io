#pragma once

#include <cstdint>
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
        IDirect3DDevice9 *device = nullptr;
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

    // GPU side copy of a back buffer, taken while the contents are still the frame that was
    // presented, so that reading them into system memory no longer has to happen before it
    struct Snapshot {
        int screen {};
        D3DSURFACE_DESC desc {};
        IDirect3DDevice9 *device = nullptr;
        SurfacePtr surface;
        uint64_t generation {};

        // when the blit was issued, so a frame left behind by a break in the request stream can
        // be recognised as stale rather than handed over
        uint64_t issued_us {};

        Snapshot() = default;
        Snapshot(Snapshot &&) noexcept = default;
        Snapshot &operator=(Snapshot &&) noexcept = default;
        Snapshot(const Snapshot &) = delete;
        Snapshot &operator=(const Snapshot &) = delete;
        ~Snapshot();
    };

    // for the present thread, between the last EndScene and Present. blits the current frame,
    // then returns the snapshot taken on the *previous* call: waiting a frame before reading
    // means the blit and its system memory transfer have already happened, so the read does not
    // stall on the GPU. costs the stream one frame of latency.
    //
    // returns nothing on the first call of a stream, and whenever the frame could not be taken,
    // which is the caller's cue to skip rather than to wait
    std::optional<Snapshot> snapshot_backbuffer(
            IDirect3DDevice9 *device,
            IDirect3DSwapChain9 *swap_chain,
            int screen);

    // the expensive half, for a thread that is not the present thread. only legal on a device
    // created with D3DCREATE_MULTITHREADED
    std::optional<BackbufferCopy> read_snapshot(Snapshot snapshot);

    // false once a device has refused to give up a render target matching its back buffer,
    // which leaves reading the back buffer directly as the only way to capture it
    bool snapshots_supported();

    // snapshot targets live in the default pool, so unlike the readback surfaces they have to
    // be gone before a Reset and not merely before the device is released
    void discard_snapshot_targets(IDirect3DDevice9 *device);

    // pooled surfaces hold references on the device; call this before releasing it
    void release_device_resources(IDirect3DDevice9 *device);
}
