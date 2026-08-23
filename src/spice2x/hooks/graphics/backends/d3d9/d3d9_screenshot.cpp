#include "d3d9_screenshot.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <external/robin_hood.h>
#include <external/fpng/fpng.h>

#include "api/capture_pump.h"
#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "misc/clipboard.h"
#include "overlay/notifications.h"
#include "util/fileutils.h"
#include "util/logging.h"
#include "util/threadpool.h"

#include "d3d9_device.h"
#include "d3d9_readback.h"

// genpath picks filenames by probing the disk, so the whole save has to be serialised:
// a name is only taken once its file exists, not when genpath hands it out
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

// a screen already read out of its surface, so nothing here touches D3D. the bytes
// are still in the surface's format; converting them is left to the encode
struct PendingWrite {
    int screen {};
    D3DFORMAT format {};
    UINT width {};
    UINT height {};
    size_t pitch {};
    std::vector<uint8_t> data;
    std::string path;
    bool saved = false;
};

struct PendingCapture {
    int screen {};
    D3DFORMAT format {};
    UINT width {};
    UINT height {};
    size_t pitch {};
    std::vector<uint8_t> data;
};

// packed 24bpp RGB, what both the png encoder and the api capture consume
constexpr size_t RGB_PIXEL_SIZE = 3;

// the formats surface_to_rgb knows how to convert; the two must stay in sync
static std::optional<size_t> surface_pixel_size(D3DFORMAT format) {
    switch (format) {
        // what back buffers are actually created as in practice
        case D3DFMT_X8R8G8B8:
        case D3DFMT_A8R8G8B8:

        // a valid display format, but no supported game has been seen presenting one
        case D3DFMT_A2R10G10B10:
            return 4;

        // valid display formats, but no supported game has been seen presenting one
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
            return 2;

        default:
            return std::nullopt;
    }
}

struct ImageSize {
    size_t row_size {};
    size_t total_size {};
};

static std::optional<ImageSize> compute_image_size(
        UINT width,
        UINT height,
        size_t bytes_per_pixel) {

    if (width == 0 || height == 0
            || width > std::numeric_limits<size_t>::max() / bytes_per_pixel) {
        return std::nullopt;
    }

    const size_t row_size = static_cast<size_t>(width) * bytes_per_pixel;
    if (height > std::numeric_limits<size_t>::max() / row_size) {
        return std::nullopt;
    }

    return ImageSize { row_size, static_cast<size_t>(height) * row_size };
}

static bool resize_pixels(std::vector<uint8_t> &pixels, size_t size) {
    try {
        pixels.resize(size);
        return true;
    } catch (const std::exception &error) {
        log_warning("graphics::d3d9", "failed to allocate image buffer: {}", error.what());
        return false;
    }
}

// the api capture stages a whole back buffer every frame, so the staging buffer
// is recycled rather than reallocated. returned buffers keep their size, which
// leaves the reuse free of a zero fill
class CaptureBuffers {
public:
    std::vector<uint8_t> take() {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->idle.empty()) {
            return {};
        }

        auto buffer = std::move(this->idle.back());
        this->idle.pop_back();
        return buffer;
    }

    void give(std::vector<uint8_t> buffer) {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->idle.size() < MAX_IDLE) {
            this->idle.push_back(std::move(buffer));
        }
    }

private:
    // one per save in flight plus one for the next capture; a full screen is
    // several megabytes, so the cap matters
    static constexpr size_t MAX_IDLE = 2;

    std::mutex mutex;
    std::vector<std::vector<uint8_t>> idle;
};

// deliberately never destroyed, so a save still running at process exit cannot
// hand a buffer back to a dead free list
CaptureBuffers &capture_buffers() {
    static CaptureBuffers *instance = new CaptureBuffers();
    return *instance;
}

// encodes get their own pool: the dispatch below already occupies a worker on its
// pool, so queueing onto that one and waiting could starve itself. never destroyed
// for the same reason as the buffers above
ThreadPool &encode_pool() {
    static auto *instance = new ThreadPool(2);
    return *instance;
}

// where a capture's pixels are converted and handed to the api. never destroyed: the read
// pool below can still be working at process exit, and it queues onto this one
ThreadPool &capture_save_pool() {
    static auto *instance = new ThreadPool(2);
    return *instance;
}

// normalize the supported D3D formats to packed 24bpp RGB. callers screen the
// format through surface_pixel_size first, so the black fill below is a fallback
void surface_to_rgb(
        D3DFORMAT format,
        UINT width,
        UINT height,
        const uint8_t *data,
        size_t pitch,
        uint8_t *pixels) {

    for (size_t row = 0; row < height; row++) {
        size_t offset_row = row * width * 3;
        switch (format) {
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
            // the 5 and 6 bit channels are widened by bit replication so that
            // full scale stays full scale
            case D3DFMT_R5G6B5: {
                auto cells = reinterpret_cast<const uint16_t *>(data + row * pitch);
                for (size_t column = 0; column < width; column++) {
                    const uint16_t cell = cells[column];
                    const uint8_t red = (cell >> 11) & 0x1F;
                    const uint8_t green = (cell >> 5) & 0x3F;
                    const uint8_t blue = cell & 0x1F;
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = (red << 3) | (red >> 2);
                    pixel[1] = (green << 2) | (green >> 4);
                    pixel[2] = (blue << 3) | (blue >> 2);
                }
                break;
            }
            case D3DFMT_X1R5G5B5:
            case D3DFMT_A1R5G5B5: {
                auto cells = reinterpret_cast<const uint16_t *>(data + row * pitch);
                for (size_t column = 0; column < width; column++) {
                    const uint16_t cell = cells[column];
                    const uint8_t red = (cell >> 10) & 0x1F;
                    const uint8_t green = (cell >> 5) & 0x1F;
                    const uint8_t blue = cell & 0x1F;
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = (red << 3) | (red >> 2);
                    pixel[1] = (green << 3) | (green >> 2);
                    pixel[2] = (blue << 3) | (blue >> 2);
                }
                break;
            }
            case D3DFMT_A2R10G10B10: {
                auto cells = reinterpret_cast<const uint32_t *>(data + row * pitch);
                for (size_t column = 0; column < width; column++) {
                    const uint32_t cell = cells[column];
                    auto pixel = &pixels[offset_row + column * 3];
                    pixel[0] = static_cast<uint8_t>((cell >> 22) & 0xFF);
                    pixel[1] = static_cast<uint8_t>((cell >> 12) & 0xFF);
                    pixel[2] = static_cast<uint8_t>((cell >> 2) & 0xFF);
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
}

} // namespace

using d3d9_readback::BackbufferCopy;

static void save_capture(PendingCapture capture) {
    const auto size = compute_image_size(capture.width, capture.height, RGB_PIXEL_SIZE);
    if (!size.has_value()) {
        capture_buffers().give(std::move(capture.data));
        graphics_capture_skip(capture.screen);
        return;
    }

    auto pixels = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[size->total_size]);
    if (!pixels) {
        log_warning("graphics::d3d9", "failed to allocate capture image buffer");
        capture_buffers().give(std::move(capture.data));
        graphics_capture_skip(capture.screen);
        return;
    }

    // a format we cannot read still has to produce a frame, or api clients stall
    if (capture.data.empty()) {
        std::memset(pixels.get(), 0, size->total_size);
    } else {
        surface_to_rgb(
                capture.format,
                capture.width,
                capture.height,
                capture.data.data(),
                capture.pitch,
                pixels.get());

        capture_buffers().give(std::move(capture.data));
    }

    graphics_capture_enqueue(capture.screen, pixels.release(), capture.width, capture.height);
}

enum class SurfaceRead {
    Ok,
    Unsupported,
    Failed,
};

// copying the surface touches D3D, so it stays on the caller's thread. the bytes come
// out in the surface's own format; converting them is plain memory work for later
static SurfaceRead read_surface_raw(
        const BackbufferCopy &copy,
        size_t &row_size,
        std::vector<uint8_t> &out) {

    const auto bytes_per_pixel = surface_pixel_size(copy.desc.Format);
    if (!bytes_per_pixel.has_value()) {
        static std::once_flag warned;
        std::call_once(warned, [&copy] {
            log_warning("graphics::d3d9",
                    "unsupported surface format {}",
                    static_cast<uint32_t>(copy.desc.Format));
        });
        return SurfaceRead::Unsupported;
    }

    const auto size = compute_image_size(copy.desc.Width, copy.desc.Height, *bytes_per_pixel);
    if (!size.has_value() || !resize_pixels(out, size->total_size)) {
        return SurfaceRead::Failed;
    }

    D3DLOCKED_RECT locked {};
    HRESULT hr = copy.surface->LockRect(&locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to lock capture surface, hr={}", FMT_HRESULT(hr));
        return SurfaceRead::Failed;
    }

    if (locked.Pitch < 0 || static_cast<size_t>(locked.Pitch) < size->row_size) {
        log_warning("graphics::d3d9", "capture surface has invalid pitch {}", locked.Pitch);
        copy.surface->UnlockRect();
        return SurfaceRead::Failed;
    }

    auto data = reinterpret_cast<const uint8_t *>(locked.pBits);
    for (size_t row = 0; row < copy.desc.Height; row++) {
        std::memcpy(
                out.data() + row * size->row_size,
                data + row * locked.Pitch,
                size->row_size);
    }

    hr = copy.surface->UnlockRect();
    if (FAILED(hr)) {
        log_warning("graphics::d3d9", "failed to unlock capture surface, hr={}", FMT_HRESULT(hr));
        return SurfaceRead::Failed;
    }

    row_size = size->row_size;
    return SurfaceRead::Ok;
}

static bool read_capture_surface(
        const BackbufferCopy &copy,
        PendingCapture &capture) {

    capture.screen = copy.screen;
    capture.format = copy.desc.Format;
    capture.width = copy.desc.Width;
    capture.height = copy.desc.Height;

    capture.data = capture_buffers().take();
    const auto result = read_surface_raw(copy, capture.pitch, capture.data);
    if (result == SurfaceRead::Ok) {
        return true;
    }

    capture_buffers().give(std::move(capture.data));
    capture.data.clear();

    // a format we cannot read is reported as a black frame rather than nothing,
    // so a client polling the api keeps getting responses
    return result == SurfaceRead::Unsupported;
}

static bool write_screenshot_png(
        const std::string &file_path,
        UINT width,
        UINT height,
        const std::vector<uint8_t> &pixels) {

    // a no-op while FPNG_NO_SSE is set, but fpng requires it before any encode
    static std::once_flag fpng_ready;
    std::call_once(fpng_ready, [] { fpng::fpng_init(); });

    log_info("graphics::d3d9", "saving screenshot to {}", file_path);

    if (!fpng::fpng_encode_image_to_file(
            file_path.c_str(),
            pixels.data(),
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            3)) {
        log_warning("graphics::d3d9", "failed to write screenshot png");
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

// games that crash or hang when the screenshot processor runs on another thread.
// D3DCREATE_MULTITHREADED is not a predictor of this; MDX omits it and threads fine
static bool image_processing_must_be_inline() {
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

    return THREAD_BAN.contains(avs::game::MODEL);
}

static void dispatch_capture_save(PendingCapture capture) {
    auto capture_process = [capture = std::move(capture)]() mutable {
        // an escape from here would cross a thread boundary and terminate
        try {
            save_capture(std::move(capture));
        } catch (const std::exception &error) {
            log_warning("graphics::d3d9", "capture save failed: {}", error.what());
        } catch (...) {
            log_warning("graphics::d3d9", "capture save failed");
        }
    };

    if (image_processing_must_be_inline()) {
        capture_process();
    } else {
        capture_save_pool().add(std::move(capture_process));
    }
}

// destroying the BackbufferCopy returns its surface to the pool, which is a device call, so
// it has to happen on whichever thread was cleared to do the read
static void read_and_dispatch_capture(int screen, BackbufferCopy copy) {
    PendingCapture capture;
    if (!read_capture_surface(copy, capture)) {
        graphics_capture_skip(screen);
        return;
    }

    dispatch_capture_save(std::move(capture));
}

// Whether the readback runs on the present thread or a pool thread trades the game's frame
// time against the risk of two threads being inside the device at once.
//
// The read is a LockRect plus a row by row memcpy of the whole back buffer: roughly 635us at
// 720p and 1270us at 1080p. On the present thread that comes straight out of the game's frame
// budget, and at 120Hz with a 60fps stream running it measured as a drop to 117fps. Moving it
// to a pool thread gave the full 120 back.
//
// Only streaming is worth that trade. It is the only path that pays the cost on every frame,
// and it is the only one the user has opted into by connecting a client. Screenshots and the
// one off api captures stay inline: they are rare enough that a single slow frame does not
// matter, and the hazard being avoided is reproduced rather than theoretical, since a pool
// thread in LockRect while the present thread sat inside GetRenderTargetData deadlocked
// DDR X2, whose device has no internal locking. Games already known to dislike threaded image
// processing are excluded as well, on the assumption that whatever breaks them applies here.
static bool capture_read_off_thread(int screen) {
    return api::capture_pump::screen_claimed(screen) && !image_processing_must_be_inline();
}

ThreadPool &capture_read_pool() {
    // one worker, so reads finish in the order they were submitted: a second worker could
    // overtake a descheduled one and enqueue a stale frame over a newer one. never destroyed,
    // so a read still running at process exit cannot touch a dead pool
    static auto *instance = new ThreadPool(1);
    return *instance;
}

// Takes the frame on the present thread as a queued GPU blit and hands the readback to a pool
// thread, so the game waits for neither. Only viable where the whole read can go off thread,
// since the back buffer is overwritten right after Present and a snapshot the present thread
// then had to read itself would cost more than reading the back buffer directly.
//
// Returns false when the frame could not be taken, including the ordinary case of the previous
// snapshot of this screen still being read, which paces capture to what the reader sustains.
static bool snapshot_capture(
        IDirect3DDevice9 *device,
        WrappedIDirect3DDevice9 *wrapped_device,
        int screen) {

    IDirect3DSwapChain9 *swap_chain = nullptr;
    const HRESULT hr = wrapped_device->get_screenshot_swap_chain(screen, &swap_chain);
    if (FAILED(hr) || swap_chain == nullptr) {
        log_warning("graphics::d3d9",
                "failed to get swap chain for screen {}, hr={}",
                screen,
                FMT_HRESULT(hr));
        return false;
    }

    auto snapshot = d3d9_readback::snapshot_backbuffer(device, swap_chain, screen);
    swap_chain->Release();

    if (!snapshot.has_value()) {
        return false;
    }

    try {
        capture_read_pool().add([screen, snapshot = std::move(*snapshot)]() mutable {
            // an escape from here would cross a thread boundary and terminate
            try {
                auto copy = d3d9_readback::read_snapshot(std::move(snapshot));
                if (!copy.has_value()) {
                    graphics_capture_skip(screen);
                    return;
                }

                read_and_dispatch_capture(screen, std::move(*copy));
            } catch (const std::exception &error) {
                log_warning("graphics::d3d9", "capture read failed: {}", error.what());
                graphics_capture_skip(screen);
            } catch (...) {
                log_warning("graphics::d3d9", "capture read failed");
                graphics_capture_skip(screen);
            }
        });
    } catch (const std::exception &) {
        // the snapshot went into the lambda before the queue could fail, so it is already
        // destroyed and its target handed back; the client just misses this frame
        return false;
    }

    return true;
}

// by this point the pixels are plain memory, so none of this needs the device
static void dispatch_screenshot_save(std::vector<PendingWrite> writes, size_t screen_count) {
    auto screenshot_process = [writes = std::move(writes), screen_count]() mutable {
        std::lock_guard<std::mutex> lock(SCREENSHOT_SAVE_M);

        std::vector<int> screens;
        screens.reserve(writes.size());
        for (const auto &write : writes) {
            screens.push_back(write.screen);
        }

        const auto base_path = graphics_screenshot_genpath(screens);
        if (base_path.empty()) {
            return;
        }

        for (auto &write : writes) {
            write.path = screenshot_path_for_screen(base_path, write.screen);
        }

        // screens missing from writes either failed to be acquired or failed to read
        size_t failed = screen_count - writes.size();

        // a throw here would otherwise reach a thread boundary and terminate
        auto encode_one = [](PendingWrite &write) {
            try {
                const auto rgb = compute_image_size(write.width, write.height, RGB_PIXEL_SIZE);
                std::vector<uint8_t> pixels;
                if (!rgb.has_value() || !resize_pixels(pixels, rgb->total_size)) {
                    write.saved = false;
                    return;
                }

                surface_to_rgb(
                        write.format,
                        write.width,
                        write.height,
                        write.data.data(),
                        write.pitch,
                        pixels.data());

                // the encode below is the long part; the raw copy is dead by now
                write.data.clear();
                write.data.shrink_to_fit();

                write.saved = write_screenshot_png(
                        write.path, write.width, write.height, pixels);
            } catch (const std::exception &error) {
                log_warning("graphics::d3d9",
                        "screenshot encode failed for {}: {}", write.path, error.what());
                write.saved = false;
            } catch (...) {
                log_warning("graphics::d3d9",
                        "screenshot encode failed for {}", write.path);
                write.saved = false;
            }
        };

        {
            // sized up front and assigned by index: storing a future must not be able
            // to throw once its task is queued, or the screen would encode twice
            std::vector<std::future<void>> pending(writes.empty() ? 0 : writes.size() - 1);
            for (size_t i = 1; i < writes.size(); i++) {
                try {
                    pending[i - 1] = encode_pool().add([&writes, &encode_one, i] {
                        encode_one(writes[i]);
                    });
                } catch (const std::exception &) {
                    // nothing to queue onto; encoding it here still makes progress
                    encode_one(writes[i]);
                }
            }

            if (!writes.empty()) {
                encode_one(writes.front());
            }

            for (auto &task : pending) {
                if (task.valid()) {
                    task.wait();
                }
            }
        }

        std::string primary_path;
        std::string notify_path;
        for (const auto &write : writes) {
            if (!write.saved) {
                failed++;
                continue;
            }
            if (notify_path.empty()) {
                notify_path = write.path;
            }
            if (write.screen == 0) {
                primary_path = write.path;
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
    };

    // genpath and the path building below allocate, so an escape from here would
    // cross a thread boundary and terminate
    auto guarded = [process = std::move(screenshot_process)]() mutable {
        try {
            process();
        } catch (const std::exception &error) {
            log_warning("graphics::d3d9", "screenshot save failed: {}", error.what());
        } catch (...) {
            log_warning("graphics::d3d9", "screenshot save failed");
        }
    };

    if (image_processing_must_be_inline()) {
        guarded();
    } else {
        static auto pool = ThreadPool(2);
        pool.add(std::move(guarded));
    }
}

static void process_image_request(
        IDirect3DDevice9 *device,
        WrappedIDirect3DDevice9 *wrapped_device,
        const ImageRequest &request) {
    const bool screenshot = request.kind == ImageRequestKind::Screenshot;

    if (!screenshot
            && wrapped_device->device_multithreaded
            && capture_read_off_thread(request.screen)
            && d3d9_readback::snapshots_supported()) {

        if (!snapshot_capture(device, wrapped_device, request.screen)) {
            graphics_capture_skip(request.screen);
        }

        return;
    }

    std::vector<int> screens { request.screen };
    if (screenshot && GRAPHICS_SCREENSHOT_SUBSCREENS) {
        screens.clear();
        wrapped_device->get_screenshot_screens(screens);
    }

    std::vector<BackbufferCopy> copies;
    copies.reserve(screens.size());
    for (const int screen : screens) {
        std::optional<BackbufferCopy> copy;

        IDirect3DSwapChain9 *swap_chain = nullptr;
        HRESULT hr = wrapped_device->get_screenshot_swap_chain(screen, &swap_chain);
        if (FAILED(hr) || swap_chain == nullptr) {
            log_warning("graphics::d3d9",
                    "failed to get swap chain for screen {}, hr={}",
                    screen,
                    FMT_HRESULT(hr));
        } else {
            // only the API capture path runs often enough to benefit from pooling
            copy = d3d9_readback::acquire_backbuffer_copy(device, swap_chain, screen, !screenshot);
            swap_chain->Release();
        }

        if (copy.has_value()) {
            copies.emplace_back(std::move(*copy));
        } else if (!screenshot) {
            graphics_capture_skip(request.screen);
            return;
        }
    }

    if (copies.empty()) {
        return;
    }

    if (!screenshot) {
        auto copy = std::move(copies.front());
        copies.clear();

        if (capture_read_off_thread(request.screen)) {
            try {
                capture_read_pool().add(
                        [screen = request.screen, copy = std::move(copy)]() mutable {
                    // an escape from here would cross a thread boundary and terminate
                    try {
                        read_and_dispatch_capture(screen, std::move(copy));
                    } catch (const std::exception &error) {
                        log_warning("graphics::d3d9", "capture read failed: {}", error.what());
                        graphics_capture_skip(screen);
                    } catch (...) {
                        log_warning("graphics::d3d9", "capture read failed");
                        graphics_capture_skip(screen);
                    }
                });
            } catch (const std::exception &) {
                // the copy went into the lambda before the queue could fail, so there is
                // nothing left to read here and the client misses this frame
                graphics_capture_skip(request.screen);
            }

            return;
        }

        read_and_dispatch_capture(request.screen, std::move(copy));
        return;
    }

    // reading a surface touches the device, and doing that off the present thread
    // has been seen to deadlock games whose device has no internal locking
    std::vector<PendingWrite> writes;
    writes.reserve(copies.size());
    for (const auto &copy : copies) {
        PendingWrite write;
        write.screen = copy.screen;
        write.format = copy.desc.Format;
        write.width = copy.desc.Width;
        write.height = copy.desc.Height;

        if (read_surface_raw(copy, write.pitch, write.data) != SurfaceRead::Ok) {
            continue;
        }

        writes.push_back(std::move(write));
    }

    copies.clear();

    dispatch_screenshot_save(std::move(writes), screens.size());
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
