#include "d3d9_screenshot.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <external/robin_hood.h>

#ifdef __GNUC__
#include <d3dx9tex.h>
#endif

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "misc/clipboard.h"
#include "overlay/notifications.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/threadpool.h"

#ifdef __GNUC__
typedef decltype(D3DXSaveSurfaceToFileA) *D3DXSaveSurfaceToFileA_t;
#else
#define D3DXIFF_PNG ((DWORD) 3)

typedef HRESULT (WINAPI *D3DXSaveSurfaceToFileA_t)(
        LPCSTR pDestFile,
        DWORD DestFormat,
        LPDIRECT3DSURFACE9 pSrcSurface,
        CONST PALETTEENTRY *pSrcPalette,
        CONST RECT *pSrcRect);
#endif

static bool ATTEMPTED_D3DX9_LOAD_LIBRARY = false;

namespace {

enum class ImageRequestKind {
    Screenshot,
    Capture,
};

struct ImageRequest {
    ImageRequestKind kind;
    int screen;
};

struct SurfaceReleaser {
    void operator()(IDirect3DSurface9 *surface) const {
        surface->Release();
    }
};

using SurfacePtr = std::unique_ptr<IDirect3DSurface9, SurfaceReleaser>;

struct BackbufferCopy {
    D3DSURFACE_DESC desc {};
    SurfacePtr surface;
};

} // namespace

static void save_capture(
        int screen,
        D3DFORMAT format,
        UINT width,
        UINT height,
        IDirect3DSurface9 *surface) {
    HRESULT hr;

    // lock surface to be able to access the data
    D3DLOCKED_RECT finished_copy {};
    hr = surface->LockRect(&finished_copy, nullptr, 0);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to lock screenshot surface, hr={}", FMT_HRESULT(hr));
        graphics_capture_skip(screen);
        return;
    }

    // normalize supported D3D formats to packed RGB for API capture
    size_t pitch = finished_copy.Pitch;
    auto data = reinterpret_cast<uint8_t *>(finished_copy.pBits);
    auto pixels = std::unique_ptr<uint8_t[]>(new uint8_t[width * height * 3]);
    for (size_t row = 0; row < height; row++) {
        size_t offset_row = row * width * 3;
        switch (format) {
            case D3DFMT_R8G8B8: {
                for (size_t column = 0; column < width; column++) {
                    auto cell = data + row * pitch + column * 3;
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = cell[0];
                    pixel[1] = cell[1];
                    pixel[2] = cell[2];
                }
                break;
            }
            case D3DFMT_X8R8G8B8:
            case D3DFMT_A8R8G8B8: {
                for (size_t column = 0; column < width; column++) {
                    auto cell = data + row * pitch + column * 4;
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = cell[2];
                    pixel[1] = cell[1];
                    pixel[2] = cell[0];
                }
                break;
            }
            case D3DFMT_X8B8G8R8:
            case D3DFMT_A8B8G8R8: {
                for (size_t column = 0; column < width; column++) {
                    auto cell = data + row * pitch + column * 4;
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = cell[0];
                    pixel[1] = cell[1];
                    pixel[2] = cell[2];
                }
                break;
            }
            default: {
                for (size_t column = 0; column < width; column++) {
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = 0;
                    pixel[1] = 0;
                    pixel[2] = 0;
                }
            }
        }
    }

    // unlock surface
    hr = surface->UnlockRect();
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to unlock screenshot surface, hr={}", FMT_HRESULT(hr));
        graphics_capture_skip(screen);
        return;
    }

    // enqueue
    graphics_capture_enqueue(screen, pixels.release(), width, height);
}

static void save_screenshot(
        const std::string &file_path,
        D3DFORMAT format,
        UINT width,
        UINT height,
        IDirect3DSurface9 *surface) {
    // 32-bit XRGB and ARGB surfaces use byte 3 as alpha; force opaque PNG output
    if (format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8 ||
        format == D3DFMT_X8B8G8R8 || format == D3DFMT_A8B8G8R8) {

        D3DLOCKED_RECT finished_copy {};
        HRESULT hr = surface->LockRect(&finished_copy, nullptr, 0);
        if (FAILED(hr)) {
            log_warning("graphics::d3d9", "failed to lock screenshot surface, hr={}", FMT_HRESULT(hr));
            return;
        }

        const size_t pitch = finished_copy.Pitch;
        auto data = reinterpret_cast<uint8_t *>(finished_copy.pBits);
        for (size_t row = 0; row < height; row++) {
            for (size_t column = 0; column < width; column++) {
                data[row * pitch + column * 4 + 3] = 255;
            }
        }

        hr = surface->UnlockRect();
        if (FAILED(hr)) {
            log_warning("graphics::d3d9", "failed to unlock screenshot surface, hr={}", FMT_HRESULT(hr));
            return;
        }
    }

    // lazy load function
    static D3DXSaveSurfaceToFileA_t D3DXSaveSurfaceToFileA_ptr = nullptr;
    if (D3DXSaveSurfaceToFileA_ptr == nullptr) {
        D3DXSaveSurfaceToFileA_ptr = libutils::try_proc<D3DXSaveSurfaceToFileA_t>("D3DXSaveSurfaceToFileA");

        // check if function was not found, likely because d3dx9 is not loaded
        if (!ATTEMPTED_D3DX9_LOAD_LIBRARY && D3DXSaveSurfaceToFileA_ptr == nullptr) {
            ATTEMPTED_D3DX9_LOAD_LIBRARY = true;

            // prefer the newest installed helper while supporting older D3DX9 runtimes
            for (size_t i = 43; i >= 24; i--) {
                auto lib_name = fmt::format("d3dx9_{}.dll", i);
                auto d3dx9 = libutils::try_library(lib_name);

                // Check if library was not found
                if (d3dx9 == nullptr) {
                    continue;
                }

                D3DXSaveSurfaceToFileA_ptr = libutils::try_proc<D3DXSaveSurfaceToFileA_t>(
                        d3dx9, "D3DXSaveSurfaceToFileA");

                // Check if function was not found
                if (D3DXSaveSurfaceToFileA_ptr == nullptr) {
                    FreeLibrary(d3dx9);
                    d3dx9 = nullptr;

                    continue;
                }

                log_info("graphics::d3d9", "found surface save function in '{}'", lib_name);
                break;
            }
        }
    }

    if (D3DXSaveSurfaceToFileA_ptr != nullptr) {

        // save to file
        log_info("graphics::d3d9", "saving screenshot to {}", file_path);
        const HRESULT save_result = D3DXSaveSurfaceToFileA_ptr(
            file_path.c_str(), D3DXIFF_PNG, surface, nullptr, nullptr);

        if (FAILED(save_result)) {
            log_warning("graphics::d3d9", "Failed to save screenshot");
            overlay::notifications::add(
                overlay::notifications::Severity::Error,
                "Screenshot failed to save");
            return;
        }

        // save to clipboard
        clipboard::copy_image(file_path);

        overlay::notifications::add(
            overlay::notifications::Severity::Success,
            fmt::format("Screenshot saved: {}", fileutils::basename(file_path)));
    } else {
        log_warning("graphics::d3d9", "Direct3D save helper function not available");
    }
}

static std::optional<BackbufferCopy> acquire_backbuffer_copy(
    IDirect3DDevice9 *device, IDirect3DSwapChain9 *sub_swap_chain, int screen) {

    HRESULT hr = S_OK;

    // TODO: verify screen is a valid swapchain

    IDirect3DSurface9 *buffer = nullptr;
    if (sub_swap_chain != nullptr && screen & 1) {
        hr = sub_swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &buffer);
    } else {
        hr = device->GetBackBuffer(screen, 0, D3DBACKBUFFER_TYPE_MONO, &buffer);
    }
    if (FAILED(hr) || buffer == nullptr) {
        log_warning("graphics::d3d9",
                "failed to get back buffer, hr={}",
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

    // TODO: cache render targets
    IDirect3DSurface9 *temp_surface = nullptr;
    hr = device->CreateRenderTarget(
            desc.Width, desc.Height, desc.Format, desc.MultiSampleType,
            desc.MultiSampleQuality, TRUE, &temp_surface, nullptr);
    if (FAILED(hr) || temp_surface == nullptr) {
        log_warning("graphics::d3d9",
                "failed to acquire temporary surface, hr={}",
                FMT_HRESULT(hr));
        buffer->Release();
        return std::nullopt;
    }

    hr = device->StretchRect(buffer, nullptr, temp_surface, nullptr, D3DTEXF_NONE);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9",
                "failed to copy back buffer contents, hr={}",
                FMT_HRESULT(hr));
        temp_surface->Release();
        buffer->Release();
        return std::nullopt;
    }

    // release original back buffer reference
    buffer->Release();

    return BackbufferCopy {
        .desc = desc,
        .surface = SurfacePtr(temp_surface),
    };
}

static void dispatch_surface_save(
        const ImageRequest &request,
        BackbufferCopy copy) {
    auto surface_process = [request, copy = std::move(copy)]() {
        switch (request.kind) {
            case ImageRequestKind::Capture:
                save_capture(
                        request.screen,
                        copy.desc.Format,
                        copy.desc.Width,
                        copy.desc.Height,
                        copy.surface.get());
                break;

            case ImageRequestKind::Screenshot: {
                auto file_path = graphics_screenshot_genpath();
                if (!file_path.empty()) {
                    save_screenshot(
                            file_path,
                            copy.desc.Format,
                            copy.desc.Width,
                            copy.desc.Height,
                            copy.surface.get());
                }
                break;
            }
        }
    };

    // list of games that crash when running the screenshot processor on another thread
    static const robin_hood::unordered_set<std::string> THREAD_BAN {
            "JMA",
#ifndef SPICE64
            // KFC only crashes under threaded processing in 32-bit builds
            "KFC",
#endif
            "KMA",
            "KLP",
            "LMA",
    };

    // run the save operation on another thread for supported games
    if (THREAD_BAN.contains(avs::game::MODEL)) {
        surface_process();
    } else {
        static auto pool = ThreadPool(2);
        pool.add(std::move(surface_process));
    }
}

static void process_image_request(
        IDirect3DDevice9 *device,
        IDirect3DSwapChain9 *sub_swap_chain,
        const ImageRequest &request) {
    auto copy = acquire_backbuffer_copy(
            device,
            sub_swap_chain,
            request.screen);
    if (!copy.has_value()) {
        if (request.kind == ImageRequestKind::Capture) {
            graphics_capture_skip(request.screen);
        }
        return;
    }

    dispatch_surface_save(request, std::move(*copy));
}

bool graphics_d3d9_process_screenshot(
        IDirect3DDevice9 *device,
        IDirect3DSwapChain9 *sub_swap_chain) {
    if (graphics_screenshot_consume()) {
        process_image_request(device, sub_swap_chain, ImageRequest {
            .kind = ImageRequestKind::Screenshot,
            .screen = 0,
        });
        return true;
    }
    return false;
}

void graphics_d3d9_process_capture(
        IDirect3DDevice9 *device,
        IDirect3DSwapChain9 *sub_swap_chain) {
    int screen = 0;
    if (graphics_capture_consume(&screen)) {
        process_image_request(device, sub_swap_chain, ImageRequest {
            .kind = ImageRequestKind::Capture,
            .screen = screen,
        });
    }
}
