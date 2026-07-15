#include "gdi_overlay.h"

#include <cstddef>

#include "util/logging.h"

namespace {

    // the back buffer matches the window DC; the software buffer has a fixed 32-bit layout
    enum class BufferType {
        TargetCompatible,
        Bgra32,
    };

    struct GdiBuffer {
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        // bitmap originally selected into the memory DC, restored before cleanup
        HGDIOBJ old_bitmap = nullptr;
        int width = 0;
        int height = 0;
    };

    // back buffer holds the complete frame; overlay buffer holds ImGui software pixels
    GdiBuffer BACK_BUFFER;
    GdiBuffer OVERLAY_BUFFER;

    void release_buffer(GdiBuffer &buffer) {
        // destroy the DC before the bitmap so cleanup is safe even if restoration fails
        if (buffer.dc != nullptr) {
            if (buffer.old_bitmap != nullptr && buffer.old_bitmap != HGDI_ERROR) {
                SelectObject(buffer.dc, buffer.old_bitmap);
            }
            DeleteDC(buffer.dc);
        }
        if (buffer.bitmap != nullptr) {
            DeleteObject(buffer.bitmap);
        }

        buffer = {};
    }

    // ensures the buffer has a memory DC with a bitmap of the requested size and type
    // selected into it. a matching allocation is reused; otherwise the old resources are
    // released and recreated. returns false if the dimensions or any GDI operation fail.
    bool ensure_buffer(
            GdiBuffer &buffer,
            HDC target_dc,
            int width,
            int height,
            BufferType type,
            const char *name) {
        if (width <= 0 || height <= 0) {
            return false;
        }
        if (buffer.dc != nullptr && buffer.bitmap != nullptr &&
            buffer.width == width && buffer.height == height) {
            return true;
        }

        // keep allocations across frames and recreate only after a size change
        release_buffer(buffer);
        buffer.dc = CreateCompatibleDC(target_dc);
        if (buffer.dc == nullptr) {
            log_warning("touch", "failed to create {} DC: {}", name, GetLastError());
            return false;
        }

        // compatible bitmaps are fast presentation targets; BGRA bitmaps accept raw pixels
        if (type == BufferType::TargetCompatible) {
            buffer.bitmap = CreateCompatibleBitmap(target_dc, width, height);
        } else {
            buffer.bitmap = CreateBitmap(width, height, 1, sizeof(uint32_t) * 8, nullptr);
        }
        if (buffer.bitmap == nullptr) {
            log_warning("touch", "failed to create {} bitmap: {}", name, GetLastError());
            release_buffer(buffer);
            return false;
        }

        buffer.old_bitmap = SelectObject(buffer.dc, buffer.bitmap);
        if (buffer.old_bitmap == nullptr || buffer.old_bitmap == HGDI_ERROR) {
            log_warning("touch", "failed to select {} bitmap: {}", name, GetLastError());
            release_buffer(buffer);
            return false;
        }

        buffer.width = width;
        buffer.height = height;
        return true;
    }

    bool update_overlay_buffer(
            HDC target_dc,
            const uint32_t *pixels,
            int width,
            int height) {
        if (pixels == nullptr ||
            !ensure_buffer(
                OVERLAY_BUFFER,
                target_dc,
                width,
                height,
                BufferType::Bgra32,
                "software overlay")) {
            return false;
        }

        // update the cached bitmap contents without reallocating the bitmap or its DC
        LONG bitmap_size = static_cast<LONG>(
            static_cast<size_t>(width) * static_cast<size_t>(height) * sizeof(uint32_t));
        if (SetBitmapBits(OVERLAY_BUFFER.bitmap, bitmap_size, pixels) != bitmap_size) {
            log_warning("touch", "failed to update software overlay bitmap: {}", GetLastError());
            return false;
        }
        return true;
    }
}

HDC touch_gdi_overlay_begin_frame(
        HDC target_dc,
        HBRUSH background_brush,
        int width,
        int height,
        const uint32_t *overlay_pixels,
        int overlay_width,
        int overlay_height) {
    if (!ensure_buffer(
            BACK_BUFFER,
            target_dc,
            width,
            height,
            BufferType::TargetCompatible,
            "overlay back buffer")) {
        return nullptr;
    }

    HDC draw_dc = BACK_BUFFER.dc;
    SetBkMode(draw_dc, TRANSPARENT);

    // start each frame from the transparent color-key background
    RECT buffer_rect {0, 0, width, height};
    FillRect(draw_dc, &buffer_rect, background_brush);

    if (update_overlay_buffer(
            target_dc,
            overlay_pixels,
            overlay_width,
            overlay_height) &&
        !BitBlt(draw_dc, 0, 0, overlay_width, overlay_height,
                OVERLAY_BUFFER.dc, 0, 0, SRCCOPY)) {
        log_warning("touch", "failed to draw software overlay bitmap: {}", GetLastError());
    }

    return draw_dc;
}

void touch_gdi_overlay_present(HDC target_dc) {
    // one full-window blit exposes the completed frame without an intermediate erase
    if (!BitBlt(target_dc, 0, 0, BACK_BUFFER.width, BACK_BUFFER.height,
                BACK_BUFFER.dc, 0, 0, SRCCOPY)) {
        log_warning("touch", "failed to present overlay back buffer: {}", GetLastError());
    }
}

void touch_gdi_overlay_release() {
    release_buffer(BACK_BUFFER);
    release_buffer(OVERLAY_BUFFER);
}
