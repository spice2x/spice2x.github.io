#include "transform.h"

#include "hooks/graphics/graphics.h"
#include "overlay/overlay.h"
#include "touch/touch.h"

namespace nativetouch::transform {

    bool is_tdj_dedicated_subscreen(HWND window) {
        return window != nullptr && GRAPHICS_WINDOWED && GRAPHICS_IIDX_WSUB &&
            window == TDJ_SUBSCREEN_WINDOW;
    }

    // convert game touch coordinates to physical screen coordinates for dedicated subscreen injection
    bool game_to_screen(HWND window, POINT *position) {
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
        return overlay::OVERLAY != nullptr &&
            overlay::OVERLAY->get_active() &&
            overlay::OVERLAY->can_transform_touch_input();
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

    // convert physical screen coordinates to game touch coordinates for a known target
    bool screen_to_game(HWND window, POINT *position) {
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

    // route hardware screen coordinates through dedicated or overlay mapping and report the result
    Result hardware_to_game(POINT *position) {
        const auto dedicated_subscreen = is_tdj_dedicated_subscreen(TDJ_SUBSCREEN_WINDOW);
        const auto active_overlay = has_active_overlay_transform();

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
