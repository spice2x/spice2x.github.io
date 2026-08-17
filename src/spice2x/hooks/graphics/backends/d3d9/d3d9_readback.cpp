#include "d3d9_readback.h"

#include <mutex>
#include <vector>

#include "hooks/graphics/graphics.h"
#include "util/logging.h"

namespace d3d9_readback {

namespace {

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

// idle surfaces are kept between captures, bucketed by layout so that screens of
// differing resolution do not evict each other. a new device drops everything,
// since system memory surfaces outlive Reset but not the device itself
class ReadbackPool {
public:
    SurfacePtr acquire(IDirect3DDevice9 *device, const D3DSURFACE_DESC &desc) {
        {
            std::lock_guard<std::mutex> lock(this->mutex);

            if (this->device != device) {
                this->buckets.clear();
                this->device = device;
            }

            auto *bucket = this->find(desc);
            if (bucket && !bucket->idle.empty()) {
                auto surface = std::move(bucket->idle.back());
                bucket->idle.pop_back();
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

        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->device != device) {
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
        }
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
};

// deliberately never destroyed: releasing D3D surfaces during static destruction
// would run after d3d9 may already be unloaded
ReadbackPool &pool() {
    static ReadbackPool *instance = new ReadbackPool();
    return *instance;
}

} // namespace

BackbufferCopy::~BackbufferCopy() {
    if (this->pooled && this->surface) {
        pool().release(this->device, std::move(this->surface));
    }
}

std::optional<BackbufferCopy> acquire_backbuffer_copy(
        IDirect3DDevice9 *device, IDirect3DSwapChain9 *swap_chain, int screen, bool pooled) {

    IDirect3DSurface9 *buffer = nullptr;
    HRESULT hr = swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &buffer);
    if (FAILED(hr) || buffer == nullptr) {
        log_warning("graphics::d3d9",
                "failed to get back buffer for screen {}, hr={}",
                screen,
                FMT_HRESULT(hr));
        return std::nullopt;
    }

    D3DSURFACE_DESC desc {};
    hr = buffer->GetDesc(&desc);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9",
                "failed to acquire back buffer descriptor, hr={}",
                FMT_HRESULT(hr));
        buffer->Release();
        return std::nullopt;
    }

    // GetRenderTargetData rejects multisampled sources, so resolve into a plain target first
    SurfacePtr resolved;
    IDirect3DSurface9 *source = buffer;
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        IDirect3DSurface9 *target = nullptr;
        hr = device->CreateRenderTarget(
                desc.Width, desc.Height, desc.Format,
                D3DMULTISAMPLE_NONE, 0, FALSE, &target, nullptr);
        if (FAILED(hr) || target == nullptr) {
            log_warning("graphics::d3d9",
                    "failed to acquire resolve target, hr={}",
                    FMT_HRESULT(hr));
            buffer->Release();
            return std::nullopt;
        }

        resolved.reset(target);

        hr = device->StretchRect(buffer, nullptr, target, nullptr, D3DTEXF_NONE);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9",
                    "failed to resolve back buffer, hr={}",
                    FMT_HRESULT(hr));
            buffer->Release();
            return std::nullopt;
        }

        source = target;
    }

    auto destination = pooled
            ? pool().acquire(device, desc)
            : create_readback_surface(device, desc);
    if (!destination) {
        buffer->Release();
        return std::nullopt;
    }

    hr = device->GetRenderTargetData(source, destination.get());
    buffer->Release();

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
