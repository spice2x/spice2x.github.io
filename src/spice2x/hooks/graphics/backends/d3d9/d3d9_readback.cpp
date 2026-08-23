#include "d3d9_readback.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "hooks/graphics/graphics.h"
#include "util/logging.h"

namespace d3d9_readback {

namespace {

// the snapshot path stamps frames with this to recognise one left behind by a break in the
// request stream
uint64_t now_us() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

SurfacePtr create_readback_surface(IDirect3DDevice9 *device, const D3DSURFACE_DESC &desc) {
    IDirect3DSurface9 *surface = nullptr;
    const HRESULT hr = device->CreateOffscreenPlainSurface(
            desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &surface, nullptr);

    if (FAILED(hr) || surface == nullptr) {
        log_warning("graphics::d3d9",
                "failed to create readback surface, hr={}",
                FMT_HRESULT(hr));
        return nullptr;
    }

    return SurfacePtr(surface);
}

size_t surface_bytes(const D3DSURFACE_DESC &desc) {
    size_t bytes_per_pixel = 4;
    switch (desc.Format) {
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
            bytes_per_pixel = 2;
            break;
        default:
            break;
    }

    return static_cast<size_t>(desc.Width) * desc.Height * bytes_per_pixel;
}

// idle surfaces are kept between captures, bucketed by layout so that screens of
// differing resolution do not evict each other. a new device drops everything,
// since system memory surfaces outlive Reset but not the device itself
class ReadbackPool {
public:
    SurfacePtr acquire(IDirect3DDevice9 *device, const D3DSURFACE_DESC &desc) {
        {
            std::lock_guard<std::mutex> lock(this->mutex);

            if (this->device != device) {
                this->drop();
                this->device = device;
            }

            auto *bucket = this->find(desc);
            if (bucket && !bucket->idle.empty()) {
                auto surface = std::move(bucket->idle.back());
                bucket->idle.pop_back();

                const size_t bytes = surface_bytes(desc);
                this->idle_bytes = this->idle_bytes > bytes ? this->idle_bytes - bytes : 0;
                return surface;
            }
        }

        return create_readback_surface(device, desc);
    }

    void release(IDirect3DDevice9 *device, SurfacePtr surface) {
        if (!surface) {
            return;
        }

        D3DSURFACE_DESC desc {};
        if (FAILED(surface->GetDesc(&desc))) {
            return;
        }

        const size_t bytes = surface_bytes(desc);

        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->device != device || this->idle_bytes + bytes > MAX_IDLE_BYTES) {
            return;
        }

        auto *bucket = this->find(desc);
        if (bucket == nullptr) {
            if (this->buckets.size() >= MAX_BUCKETS) {
                return;
            }

            this->buckets.push_back(Bucket { desc.Width, desc.Height, desc.Format, {} });
            bucket = &this->buckets.back();
        }

        if (bucket->idle.size() < MAX_IDLE_PER_BUCKET) {
            bucket->idle.push_back(std::move(surface));
            this->idle_bytes += bytes;
        }
    }

    // every cached surface holds a reference on the device, so they have to go
    // before it does or the device never reaches a zero reference count
    void clear_device(IDirect3DDevice9 *device) {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->device != device) {
            return;
        }

        this->drop();
        this->device = nullptr;
    }

private:
    struct Bucket {
        UINT width;
        UINT height;
        D3DFORMAT format;
        std::vector<SurfacePtr> idle;
    };

    static constexpr size_t MAX_BUCKETS = GRAPHICS_CAPTURE_SCREEN_NO;

    // one returning surface plus one for the next capture; a full screen surface
    // is several megabytes, so the cap matters
    static constexpr size_t MAX_IDLE_PER_BUCKET = 2;

    // a 4K surface is 33MB, so the per bucket count alone does not bound this
    static constexpr size_t MAX_IDLE_BYTES = 64u * 1024 * 1024;

    void drop() {
        this->buckets.clear();
        this->idle_bytes = 0;
    }

    Bucket *find(const D3DSURFACE_DESC &desc) {
        for (auto &bucket : this->buckets) {
            if (bucket.width == desc.Width
                    && bucket.height == desc.Height
                    && bucket.format == desc.Format) {
                return &bucket;
            }
        }

        return nullptr;
    }

    std::mutex mutex;
    std::vector<Bucket> buckets;
    IDirect3DDevice9 *device = nullptr;
    size_t idle_bytes = 0;
};

// deliberately never destroyed: releasing D3D surfaces during static destruction
// would run after d3d9 may already be unloaded
ReadbackPool &pool() {
    static ReadbackPool *instance = new ReadbackPool();
    return *instance;
}

// the back buffer plus the checks every caller has to make before copying out of it
SurfacePtr open_backbuffer(IDirect3DSwapChain9 *swap_chain, int screen, D3DSURFACE_DESC &desc) {
    IDirect3DSurface9 *buffer = nullptr;
    HRESULT hr = swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &buffer);
    if (FAILED(hr) || buffer == nullptr) {
        log_warning("graphics::d3d9",
                "failed to get back buffer for screen {}, hr={}",
                screen,
                FMT_HRESULT(hr));
        return nullptr;
    }

    SurfacePtr surface(buffer);

    hr = surface->GetDesc(&desc);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9",
                "failed to acquire back buffer descriptor, hr={}",
                FMT_HRESULT(hr));
        return nullptr;
    }

    // GetRenderTargetData rejects multisampled sources. no supported game has been
    // seen presenting one, so resolving is left unimplemented rather than untested
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        static std::once_flag warned;
        std::call_once(warned, [&desc] {
            log_warning("graphics::d3d9",
                    "back buffer is multisampled ({}), screenshots and capture are unsupported",
                    static_cast<uint32_t>(desc.MultiSampleType));
        });
        return nullptr;
    }

    return surface;
}

SurfacePtr create_snapshot_target(IDirect3DDevice9 *device, const D3DSURFACE_DESC &desc) {
    IDirect3DSurface9 *surface = nullptr;

    // matching the back buffer keeps the blit a straight copy and leaves the pixels in the
    // format the conversion step would have seen without the detour
    const HRESULT hr = device->CreateRenderTarget(
            desc.Width, desc.Height, desc.Format,
            D3DMULTISAMPLE_NONE, 0, FALSE, &surface, nullptr);

    if (FAILED(hr) || surface == nullptr) {
        log_warning("graphics::d3d9",
                "failed to create snapshot target, hr={}",
                FMT_HRESULT(hr));
        return nullptr;
    }

    return SurfacePtr(surface);
}

// two render targets per screen, reused across frames. two because one holds the frame waiting
// out its deferral while the other takes the next one; a read only holds its target until the
// pixels are in system memory, which is far shorter than the gap between requests.
//
// unlike the readback surfaces these live in the default pool, so they have to be gone before a
// Reset and not merely before the device is released
class SnapshotTargets {
public:
    SurfacePtr acquire(
            IDirect3DDevice9 *device,
            int screen,
            const D3DSURFACE_DESC &desc,
            uint64_t *out_generation) {

        if (screen < 0 || screen >= static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO)) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(this->mutex);

        if (this->device != device) {
            this->drop();
            this->device = device;
            this->usable = true;
        }

        auto &entry = this->screens[screen];

        Slot *free_slot = nullptr;
        for (size_t i = 0; i < SLOTS_PER_SCREEN; i++) {
            auto &candidate = entry.slots[(entry.next + i) % SLOTS_PER_SCREEN];
            if (!candidate.busy) {
                free_slot = &candidate;
                entry.next = (entry.next + i + 1) % SLOTS_PER_SCREEN;
                break;
            }
        }

        if (free_slot == nullptr) {
            return nullptr;
        }

        auto &slot = *free_slot;

        if (slot.target
                && (slot.width != desc.Width
                        || slot.height != desc.Height
                        || slot.format != desc.Format)) {
            slot.target.reset();
        }

        if (!slot.target) {
            slot.target = create_snapshot_target(device, desc);
            if (!slot.target) {
                // a back buffer format the device will not give us a render target for. the
                // inline path can still read it, so stop trying rather than lose the stream
                this->usable = false;
                return nullptr;
            }

            slot.width = desc.Width;
            slot.height = desc.Height;
            slot.format = desc.Format;
        }

        slot.busy = true;
        *out_generation = this->current_generation;

        slot.target->AddRef();
        return SurfacePtr(slot.target.get());
    }

    // holds the fresh snapshot back and returns the one from the previous request, which by now
    // has had a full frame for its blit and transfer to land
    std::optional<Snapshot> rotate(int screen, std::optional<Snapshot> fresh) {
        if (screen < 0 || screen >= static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO)) {
            return std::nullopt;
        }

        const uint64_t now = now_us();
        std::optional<Snapshot> previous;

        {
            std::lock_guard<std::mutex> lock(this->mutex);
            previous = std::move(this->pending[screen]);
            this->pending[screen] = std::move(fresh);
        }

        // a frame waits here for the next request rather than for a deadline, so a gap in
        // requests, a client reconnecting most obviously, would otherwise hand the new session
        // a frame from before the gap. showing a stale frame is worse than showing none
        if (previous.has_value() && now - previous->issued_us > MAX_DEFERRAL_US) {
            return std::nullopt;
        }

        return previous;
    }

    // a reader whose target was already thrown away must not free the slot, or it would free
    // the snapshot that replaced it while that one is still being read
    void finish(int screen, uint64_t generation, IDirect3DSurface9 *target) {
        if (screen < 0 || screen >= static_cast<int>(GRAPHICS_CAPTURE_SCREEN_NO)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (generation != this->current_generation) {
                return;
            }

            this->free_slot(screen, target);
        }

        this->idle.notify_all();
    }

    bool still_current(uint64_t generation) {
        std::lock_guard<std::mutex> lock(this->mutex);
        return generation == this->current_generation;
    }

    bool is_usable() {
        std::lock_guard<std::mutex> lock(this->mutex);
        return this->usable;
    }

    // Reset fails outright while the device still owns default pool resources, so dropping our
    // own references is not enough and any read in flight has to finish first. the wait is
    // bounded because a failed Reset is something games retry and a stalled present thread is
    // not something they survive
    void discard(IDirect3DDevice9 *device) {
        // destroyed after the lock is released, since dropping a snapshot calls back in here
        std::array<std::optional<Snapshot>, GRAPHICS_CAPTURE_SCREEN_NO> stale;

        {
            std::unique_lock<std::mutex> lock(this->mutex);
            if (this->device != nullptr && this->device != device) {
                return;
            }

            // deferred frames are abandoned rather than waited for; only a read that is already
            // running has to be allowed to finish
            for (auto &held : this->pending) {
                if (held.has_value() && held->surface) {
                    this->free_slot(held->screen, held->surface.get());
                }
            }

            stale = std::move(this->pending);
            this->pending = {};

            const bool drained = this->idle.wait_for(
                    lock,
                    std::chrono::milliseconds(100),
                    [this] { return !this->any_busy(); });

            if (!drained) {
                log_warning("graphics::d3d9",
                        "capture snapshot still in flight, discarding its target anyway");
            }

            this->drop();
            this->device = nullptr;
        }
    }

private:
    static constexpr size_t SLOTS_PER_SCREEN = 2;

    // generous next to the frame interval this is meant to bridge, so that ordinary jitter
    // never trips it and only a real break in the request stream does
    static constexpr uint64_t MAX_DEFERRAL_US = 250'000;

    struct Slot {
        SurfacePtr target;
        UINT width = 0;
        UINT height = 0;
        D3DFORMAT format = D3DFMT_UNKNOWN;
        bool busy = false;
    };

    struct Screen {
        std::array<Slot, SLOTS_PER_SCREEN> slots;
        size_t next = 0;
    };

    void free_slot(int screen, IDirect3DSurface9 *target) {
        for (auto &slot : this->screens[screen].slots) {
            if (slot.target.get() == target) {
                slot.busy = false;
                return;
            }
        }
    }

    bool any_busy() const {
        for (const auto &entry : this->screens) {
            for (const auto &slot : entry.slots) {
                if (slot.busy) {
                    return true;
                }
            }
        }

        return false;
    }

    void drop() {
        for (auto &entry : this->screens) {
            for (auto &slot : entry.slots) {
                slot.target.reset();
                slot.busy = false;
            }

            entry.next = 0;
        }

        this->current_generation++;
    }

    std::mutex mutex;
    std::condition_variable idle;
    std::array<Screen, GRAPHICS_CAPTURE_SCREEN_NO> screens;
    std::array<std::optional<Snapshot>, GRAPHICS_CAPTURE_SCREEN_NO> pending;
    IDirect3DDevice9 *device = nullptr;
    uint64_t current_generation = 1;
    bool usable = true;
};

// never destroyed, for the same reason the readback pool is not
SnapshotTargets &targets() {
    static SnapshotTargets *instance = new SnapshotTargets();
    return *instance;
}

} // namespace

void release_device_resources(IDirect3DDevice9 *device) {
    targets().discard(device);
    pool().clear_device(device);
}

void discard_snapshot_targets(IDirect3DDevice9 *device) {
    targets().discard(device);
}

bool snapshots_supported() {
    return targets().is_usable();
}

BackbufferCopy::~BackbufferCopy() {
    if (this->pooled && this->surface) {
        pool().release(this->device, std::move(this->surface));
    }
}

Snapshot::~Snapshot() {
    // still holding the target means the read never ran, and the slot would otherwise stay
    // marked busy and take the screen out of capture permanently
    if (this->surface) {
        targets().finish(this->screen, this->generation, this->surface.get());
    }
}

namespace {

std::optional<Snapshot> take_snapshot(
        IDirect3DDevice9 *device, IDirect3DSwapChain9 *swap_chain, int screen) {

    const uint64_t started_us = now_us();

    D3DSURFACE_DESC desc {};
    auto buffer = open_backbuffer(swap_chain, screen, desc);
    if (!buffer) {
        return std::nullopt;
    }

    uint64_t generation = 0;
    auto target = targets().acquire(device, screen, desc, &generation);
    if (!target) {
        return std::nullopt;
    }

    // built before the blit so that a failure below hands the slot back through the destructor
    Snapshot snapshot;
    snapshot.screen = screen;
    snapshot.desc = desc;
    snapshot.device = device;
    snapshot.surface = std::move(target);
    snapshot.generation = generation;
    snapshot.issued_us = started_us;

    // the point of the whole exercise: this is queued rather than waited on, so the game pays
    // for issuing the copy and not for it completing. identical size and format, so there is
    // no filtering to ask for
    const HRESULT hr = device->StretchRect(
            buffer.get(), nullptr, snapshot.surface.get(), nullptr, D3DTEXF_NONE);

    if (FAILED(hr)) {
        log_warning("graphics::d3d9",
                "failed to snapshot back buffer for screen {}, hr={}",
                screen,
                FMT_HRESULT(hr));
        return std::nullopt;
    }

    return snapshot;
}

} // namespace

std::optional<Snapshot> snapshot_backbuffer(
        IDirect3DDevice9 *device, IDirect3DSwapChain9 *swap_chain, int screen) {

    return targets().rotate(screen, take_snapshot(device, swap_chain, screen));
}

std::optional<BackbufferCopy> read_snapshot(Snapshot snapshot) {
    if (!snapshot.surface) {
        return std::nullopt;
    }

    // a Reset between the blit and now means the target no longer holds the captured frame
    if (!targets().still_current(snapshot.generation)) {
        return std::nullopt;
    }

    auto destination = pool().acquire(snapshot.device, snapshot.desc);
    if (!destination) {
        return std::nullopt;
    }

    const HRESULT hr = snapshot.device->GetRenderTargetData(
            snapshot.surface.get(), destination.get());

    // the target is reusable as soon as the pixels are in system memory. dropping the
    // reference before freeing the slot keeps the destructor from freeing it twice
    const int screen = snapshot.screen;
    const uint64_t generation = snapshot.generation;
    IDirect3DSurface9 *target = snapshot.surface.get();
    snapshot.surface.reset();
    targets().finish(screen, generation, target);

    if (FAILED(hr)) {
        log_warning("graphics::d3d9",
                "failed to read snapshot contents, hr={}",
                FMT_HRESULT(hr));
        pool().release(snapshot.device, std::move(destination));
        return std::nullopt;
    }

    BackbufferCopy copy;
    copy.screen = screen;
    copy.desc = snapshot.desc;
    copy.device = snapshot.device;
    copy.surface = std::move(destination);
    copy.pooled = true;

    return copy;
}

std::optional<BackbufferCopy> acquire_backbuffer_copy(
        IDirect3DDevice9 *device, IDirect3DSwapChain9 *swap_chain, int screen, bool pooled) {

    D3DSURFACE_DESC desc {};
    auto buffer = open_backbuffer(swap_chain, screen, desc);
    if (!buffer) {
        return std::nullopt;
    }

    auto destination = pooled
            ? pool().acquire(device, desc)
            : create_readback_surface(device, desc);
    if (!destination) {
        return std::nullopt;
    }

    const HRESULT hr = device->GetRenderTargetData(buffer.get(), destination.get());

    if (FAILED(hr)) {
        log_warning("graphics::d3d9",
                "failed to copy back buffer contents, hr={}",
                FMT_HRESULT(hr));
        if (pooled) {
            pool().release(device, std::move(destination));
        }
        return std::nullopt;
    }

    BackbufferCopy copy;
    copy.screen = screen;
    copy.desc = desc;
    copy.device = device;
    copy.surface = std::move(destination);
    copy.pooled = pooled;

    return copy;
}
}
