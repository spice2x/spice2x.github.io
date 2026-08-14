#include "d3d9_screenshot.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

#include "d3d9_device.h"

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

// genpath picks free filenames by probing the disk, so only one save may run at a time
static std::mutex SCREENSHOT_SAVE_M;

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
    int screen {};
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

static bool save_screenshot(
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
            return false;
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
            return false;
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

    if (D3DXSaveSurfaceToFileA_ptr == nullptr) {
        log_warning("graphics::d3d9", "Direct3D save helper function not available");
        return false;
    }

    // save to file
    log_info("graphics::d3d9", "saving screenshot to {}", file_path);
    const HRESULT save_result = D3DXSaveSurfaceToFileA_ptr(
        file_path.c_str(), D3DXIFF_PNG, surface, nullptr, nullptr);

    if (FAILED(save_result)) {
        log_warning("graphics::d3d9", "Failed to save screenshot");
        return false;
    }

    return true;
}

// screen 0 keeps the plain name so existing tooling and the clipboard copy are unaffected
static std::string screenshot_path_for_screen(const std::string &primary_path, int screen) {
    if (screen == 0) {
        return primary_path;
    }

    const std::filesystem::path path(primary_path);
    return (path.parent_path() /
            fmt::format("{}_{}{}", path.stem().string(), screen, path.extension().string()))
            .string();
}

static std::optional<BackbufferCopy> acquire_backbuffer_copy(
    IDirect3DDevice9 *device, WrappedIDirect3DDevice9 *wrapped_device, int screen) {

    IDirect3DSwapChain9 *swap_chain = nullptr;
    HRESULT hr = wrapped_device->get_screenshot_swap_chain(screen, &swap_chain);
    if (FAILED(hr) || swap_chain == nullptr) {
        log_warning("graphics::d3d9",
                "failed to get swap chain for screen {}, hr={}",
                screen,
                FMT_HRESULT(hr));
        return std::nullopt;
    }

    IDirect3DSurface9 *buffer = nullptr;
    hr = swap_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &buffer);
    swap_chain->Release();
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
        .screen = screen,
        .desc = desc,
        .surface = SurfacePtr(temp_surface),
    };
}

static void dispatch_surface_save(
        const ImageRequest &request,
        std::vector<BackbufferCopy> copies,
        size_t screen_count) {
    auto surface_process = [request, screen_count, copies = std::move(copies)]() {
        switch (request.kind) {
            case ImageRequestKind::Capture: {
                const auto &copy = copies.front();
                save_capture(
                        request.screen,
                        copy.desc.Format,
                        copy.desc.Width,
                        copy.desc.Height,
                        copy.surface.get());
                break;
            }

            case ImageRequestKind::Screenshot: {
                std::lock_guard<std::mutex> lock(SCREENSHOT_SAVE_M);

                std::vector<int> screens;
                screens.reserve(copies.size());
                for (const auto &copy : copies) {
                    screens.push_back(copy.screen);
                }

                const auto base_path = graphics_screenshot_genpath(screens);
                if (base_path.empty()) {
                    break;
                }

                // screens missing from copies already failed to be acquired
                size_t failed = screen_count - copies.size();
                std::string primary_path;
                std::string notify_path;
                for (const auto &copy : copies) {
                    const auto path = screenshot_path_for_screen(base_path, copy.screen);
                    if (!save_screenshot(
                            path,
                            copy.desc.Format,
                            copy.desc.Width,
                            copy.desc.Height,
                            copy.surface.get())) {
                        failed++;
                        continue;
                    }
                    if (notify_path.empty()) {
                        notify_path = path;
                    }
                    if (copy.screen == 0) {
                        primary_path = path;
                    }
                }

                // only the primary screen goes to the clipboard, but any saved file is a success
                if (!primary_path.empty()) {
                    clipboard::copy_image(primary_path);
                }
                if (!notify_path.empty()) {
                    overlay::notifications::add(
                        overlay::notifications::Severity::Success,
                        fmt::format("Screenshot saved: {}", fileutils::basename(notify_path)));
                } else {
                    overlay::notifications::add(
                        overlay::notifications::Severity::Error,
                        "Screenshot failed to save");
                }

                if (failed > 0) {
                    log_warning("graphics::d3d9", "{} screenshot screen(s) missing", failed);
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
        WrappedIDirect3DDevice9 *wrapped_device,
        const ImageRequest &request) {
    std::vector<int> screens { request.screen };
    if (request.kind == ImageRequestKind::Screenshot && GRAPHICS_SCREENSHOT_SUBSCREENS) {
        screens.clear();
        wrapped_device->get_screenshot_screens(screens);
    }

    std::vector<BackbufferCopy> copies;
    copies.reserve(screens.size());
    for (const int screen : screens) {
        auto copy = acquire_backbuffer_copy(device, wrapped_device, screen);
        if (copy.has_value()) {
            copies.emplace_back(std::move(*copy));
        } else if (request.kind == ImageRequestKind::Capture) {
            graphics_capture_skip(request.screen);
            return;
        }
    }

    if (copies.empty()) {
        return;
    }

    dispatch_surface_save(request, std::move(copies), screens.size());
}

void graphics_d3d9_process_screenshot(
        IDirect3DDevice9 *device,
        WrappedIDirect3DDevice9 *wrapped_device) {
    if (graphics_screenshot_consume()) {
        process_image_request(device, wrapped_device, ImageRequest {
            .kind = ImageRequestKind::Screenshot,
            .screen = 0,
        });
    }
}

void graphics_d3d9_process_capture(
        IDirect3DDevice9 *device,
        WrappedIDirect3DDevice9 *wrapped_device) {
    int screen = 0;
    if (graphics_capture_consume(&screen)) {
        process_image_request(device, wrapped_device, ImageRequest {
            .kind = ImageRequestKind::Capture,
            .screen = screen,
        });
    }
}
