#include "transform.h"

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "settings.h"
#include "touch/touch.h"

namespace nativetouch::transform {

    static bool game_client_to_screen(HWND window, POINT *position) {
        RECT client_rect {};
        if (window == nullptr ||
            !GetClientRect(window, &client_rect) ||
            client_rect.right <= 0 || client_rect.bottom <= 0 ||
            !PtInRect(&client_rect, *position)) {
            return false;
        }

        return ClientToScreen(window, position) != FALSE;
    }

    static bool screen_to_game_client(HWND window, POINT *position) {
        RECT client_rect {};
        if (window == nullptr ||
            !GetClientRect(window, &client_rect) ||
            client_rect.right <= 0 || client_rect.bottom <= 0 ||
            !ScreenToClient(window, position) ||
            !PtInRect(&client_rect, *position)) {
            return false;
        }

        return true;
    }

    bool is_tdj_dedicated_subscreen(HWND window) {
        return window != nullptr && GRAPHICS_WINDOWED && GRAPHICS_IIDX_WSUB &&
            window == TDJ_SUBSCREEN_WINDOW;
    }

    // the GITADORA arena SMALL window is a real touch panel; it only exists in the
    // window modes that do not draw the subscreen in the overlay
    static bool has_gfdm_dedicated_subscreen() {
        return GFDM_SUBSCREEN_WINDOW != nullptr;
    }

    // convert game touch coordinates to Windows desktop coordinates
    bool game_to_screen(HWND window, POINT *position) {
        if (settings::SYNTHETIC_TOUCH_USES_CLIENT_COORDINATES) {
            return game_client_to_screen(window, position);
        }

        if (!is_tdj_dedicated_subscreen(window)) {
            return true;
        }

        RECT client_rect {};
        if (!GetClientRect(window, &client_rect) ||
            client_rect.right <= 0 || client_rect.bottom <= 0 ||
            SPICETOUCH_TOUCH_WIDTH <= 0 || SPICETOUCH_TOUCH_HEIGHT <= 0) {
            return false;
        }

        position->x = MulDiv(
            position->x - SPICETOUCH_TOUCH_X,
            client_rect.right,
            SPICETOUCH_TOUCH_WIDTH);
        position->y = MulDiv(
            position->y - SPICETOUCH_TOUCH_Y,
            client_rect.bottom,
            SPICETOUCH_TOUCH_HEIGHT);
        return ClientToScreen(window, position) != FALSE;
    }

    static bool has_active_overlay_transform() {
        // the arena SMALL window is the touch surface whenever it exists, so the
        // subscreen overlay must not claim touch input in those window modes
        if (has_gfdm_dedicated_subscreen()) {
            return false;
        }

        return overlay::OVERLAY != nullptr &&
            overlay::OVERLAY->get_active() &&
            overlay::OVERLAY->has_subscreen_touch_transform();
    }

    static bool transform_overlay_touch_position(POINT *position) {
        // convert physical screen coordinates to the window-relative coordinates the overlay expects
        if (GRAPHICS_WINDOWED) {
            position->x -= SPICETOUCH_TOUCH_X;
            position->y -= SPICETOUCH_TOUCH_Y;
        }

        // ask the overlay to do the game-specific translation
        return overlay::OVERLAY->transform_touch_point(&position->x, &position->y);
    }

    // the digitizer is mapped to the zero-based primary display, while SDVX
    // still expects portrait coordinates when its image is rendered in landscape:
    // (x, y) -> (width * (1 - y / height), height * x / width).
    static bool transform_sdvx_landscape_touch_position(POINT *position) {
        const auto landscape_width = static_cast<LONG>(GRAPHICS_FS_CUSTOM_RESOLUTION.has_value() ?
            GRAPHICS_FS_CUSTOM_RESOLUTION.value().first : GRAPHICS_FS_ORIGINAL_HEIGHT);
        const auto landscape_height = static_cast<LONG>(GRAPHICS_FS_CUSTOM_RESOLUTION.has_value() ?
            GRAPHICS_FS_CUSTOM_RESOLUTION.value().second : GRAPHICS_FS_ORIGINAL_WIDTH);
        if (landscape_width <= 0 || landscape_height <= 0) {
            return false;
        }

        const auto input_x = position->x;
        position->x = landscape_width -
            MulDiv(position->y, landscape_width, landscape_height);
        position->y = MulDiv(input_x, landscape_height, landscape_width);
        return true;
    }

    // convert physical screen coordinates to game touch coordinates for a known target
    bool screen_to_game(HWND window, POINT *position) {
        if (settings::SYNTHETIC_TOUCH_USES_CLIENT_COORDINATES) {
            return screen_to_game_client(window, position);
        }

        // scale the resized IIDX subscreen client area into the game's touch-display coordinates
        if (is_tdj_dedicated_subscreen(window)) {
            RECT client_rect {};
            if (!GetClientRect(window, &client_rect) ||
                client_rect.right <= 0 || client_rect.bottom <= 0 ||
                SPICETOUCH_TOUCH_WIDTH <= 0 || SPICETOUCH_TOUCH_HEIGHT <= 0) {
                return false;
            }

            if (!ScreenToClient(window, position)) {
                return false;
            }
            if (!PtInRect(&client_rect, *position)) {
                return false;
            }
            position->x = SPICETOUCH_TOUCH_X +
                MulDiv(position->x, SPICETOUCH_TOUCH_WIDTH, client_rect.right);
            position->y = SPICETOUCH_TOUCH_Y +
                MulDiv(position->y, SPICETOUCH_TOUCH_HEIGHT, client_rect.bottom);
            return true;
        }

        // check if subscreen overlay is active and can transform the touch point;
        // if not, the touch point is valid as-is
        if (!has_active_overlay_transform()) {
            return true;
        }

        // ask the overlay to transform the touch point into game coordinates
        return transform_overlay_touch_position(position);
    }

    bool mouse_to_game(HWND window, POINT *position) {

        // exception: iidx tdj dedicated subscreen window is allowed
        if (is_tdj_dedicated_subscreen(window)) {
            return screen_to_game(window, position);
        }

        // exception: sdvx windowed subscreen does not use the subscreen overlay transform
        if (GRAPHICS_WINDOWED && window == SDVX_SUBSCREEN_WINDOW) {
            POINT client_position = *position;
            return screen_to_game_client(window, &client_position);
        }

        // exception: the arena SMALL window is the touch panel, so accept the mouse there
        // (and only there) with the coordinates a real contact on it would produce
        if (has_gfdm_dedicated_subscreen()) {
            if (window != GFDM_SUBSCREEN_WINDOW) {
                return false;
            }

            POINT client_position = *position;
            return screen_to_game_client(window, &client_position);
        }

        // if this game has a subscreen overlay that can transform touch input
        // but the window is hidden or not under the cursor, reject mouse-as-touch
        // (e.g., iidx/sdvx are rejected here, but nostalgia is allowed)
        if (overlay::OVERLAY != nullptr &&
            overlay::OVERLAY->has_subscreen_touch_transform() &&
            !overlay::OVERLAY->accepts_subscreen_mouse_input()) {
            return false;
        }

        return screen_to_game(window, position);
    }

    // route hardware screen coordinates through dedicated or overlay mapping and report the result
    Result hardware_to_game(POINT *position) {
        const auto dedicated_subscreen = is_tdj_dedicated_subscreen(TDJ_SUBSCREEN_WINDOW);
        const auto active_overlay = has_active_overlay_transform();

        // special case for SDVX landscape mode
        if (!dedicated_subscreen && !active_overlay &&
            GRAPHICS_FS_ORIENTATION_SWAP && avs::game::is_model("KFC")) {
            return transform_sdvx_landscape_touch_position(position) ?
                Result::Transformed : Result::Rejected;
        }

        // no dedicated subscreen or active overlay mapping; pass the point through unchanged
        if (!dedicated_subscreen && !active_overlay) {
            return Result::Unchanged;
        }

        // route through the dedicated subscreen when active, otherwise through the overlay
        const auto valid = screen_to_game(
                dedicated_subscreen ? TDJ_SUBSCREEN_WINDOW : nullptr,
                position);

        // reject out-of-bounds points and any coordinate conversion failure
        return valid ? Result::Transformed : Result::Rejected;
    }
}
