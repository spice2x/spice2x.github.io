#pragma once

#include <cstdint>
#include <windows.h>

// prepares a complete offscreen frame and returns its drawing DC; returns null on failure
HDC touch_gdi_overlay_begin_frame(
    HDC target_dc,
    HBRUSH background_brush,
    int width,
    int height,
    const uint32_t *overlay_pixels,
    int overlay_width,
    int overlay_height);

// presents the frame prepared by the most recent successful begin call
void touch_gdi_overlay_present(HDC target_dc);

// releases all cached GDI resources
void touch_gdi_overlay_release();
