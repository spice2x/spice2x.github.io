#include "jb_touch.h"

#include <windows.h>
#include <cmath>
#include <vector>

#include "avs/game.h"
#include "hooks/graphics/graphics.h"
#include "launcher/launcher.h"
#include "touch/touch.h"
#include "rawinput/touch.h"
#include "util/logging.h"
#include "util/utils.h"

// touch layout: a 4x4 grid of 160px buttons separated by 37 / 38 / 37 px gaps
// (752px across). the first button's top-left is at (8, 602) in portrait and
// (6, 8) in landscape.
#define JB_BUTTON_SIZE 160

// improved mode snaps each touch to the nearest button within this reach; a touch
// farther than this from every button registers nothing. must be >= the diagonal
// half of the widest gap (~27px) so the grid centre still reaches a button.
#define JB_TOUCH_RADIUS 30

// plus mode marks every button within this larger reach, so a finger pressed on an
// edge or in a gap triggers all the buttons it overlaps (like the mobile game)
#define JB_TOUCH_RADIUS_PLUS 40

namespace games::jb {

    // touch state
    JubeatTouchAlgorithm TOUCH_ALGORITHM = Improved;
    JubeatTouchDebugMode TOUCH_DEBUG_OVERLAY = JbTouchDebugAuto;
    static bool TOUCH_ENABLE = false;
    static bool TOUCH_ATTACHED = false;
    static bool IS_PORTRAIT = true;
    static std::vector<TouchPoint> TOUCH_POINTS;
    bool TOUCH_STATE[16];

    // --- touch geometry ------------------------------------------------------
    // gaps between the four buttons along one axis (the middle gap is 1px wider)
    static const int JB_BUTTON_GAPS[3] = { 37, 38, 37 };

    struct AxisGeometry {
        int button[4]; // left/top edge of each button
    };

    // left/top edges of the four buttons along one axis, starting at `first`
    static AxisGeometry axis_geometry(int first) {
        AxisGeometry g {};
        g.button[0] = first;
        for (int i = 1; i < 4; i++) {
            g.button[i] = g.button[i - 1] + JB_BUTTON_SIZE + JB_BUTTON_GAPS[i - 1];
        }
        return g;
    }

    // button edges for the current orientation
    static void touch_geometry(AxisGeometry &gx, AxisGeometry &gy) {
        if (IS_PORTRAIT) {
            gx = axis_geometry(8);
            gy = axis_geometry(602);
        } else {
            gx = axis_geometry(6);
            gy = axis_geometry(8);
        }
    }

    // distance from `p` to the range [lo, hi] (0 when inside)
    static int axis_distance(int p, int lo, int hi) {
        if (p < lo) return lo - p;
        if (p > hi) return p - hi;
        return 0;
    }

    // index (0..15) of the nearest button to (px, py) within `radius`, or -1 if none;
    // shared by detection and the debug overlay so both agree which button a touch hits
    static int nearest_button(int px, int py, const AxisGeometry &gx, const AxisGeometry &gy, int radius) {
        int best_index = -1;
        int best_dist = 0;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                int dx = axis_distance(px, gx.button[c], gx.button[c] + JB_BUTTON_SIZE);
                int dy = axis_distance(py, gy.button[r], gy.button[r] + JB_BUTTON_SIZE);
                int dist = dx * dx + dy * dy;
                if (dist <= radius * radius && (best_index < 0 || dist < best_dist)) {
                    best_dist = dist;
                    best_index = r * 4 + c;
                }
            }
        }
        return best_index;
    }

    // detection reach for the current algorithm (0 = register only inside a button)
    static int touch_radius() {
        switch (TOUCH_ALGORITHM) {
            case AcAccurate:
                return 0;
            case Plus:
                return JB_TOUCH_RADIUS_PLUS;
            default:
                return JB_TOUCH_RADIUS;
        }
    }

    // mark the buttons a touch at (px, py) hits: only the nearest within `radius`, or
    // every button within `radius` when `multi` (edge/gap presses trigger several)
    static void mark_buttons(int px, int py, const AxisGeometry &gx, const AxisGeometry &gy,
                             int radius, bool multi) {
        if (!multi) {
            int index = nearest_button(px, py, gx, gy, radius);
            if (index >= 0) {
                TOUCH_STATE[index] = true;
            }
            return;
        }
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                int dx = axis_distance(px, gx.button[c], gx.button[c] + JB_BUTTON_SIZE);
                int dy = axis_distance(py, gy.button[r], gy.button[r] + JB_BUTTON_SIZE);
                if (dx * dx + dy * dy <= radius * radius) {
                    TOUCH_STATE[r * 4 + c] = true;
                }
            }
        }
    }

    void touch_update() {

        if (!TOUCH_ENABLE) {
            return;
        }

        // one-time touch window attach
        if (!TOUCH_ATTACHED) {

            // find the game window: prefer the foreground window, else search by
            // title (the model name prefixes the window title in every version)
            HWND wnd = GetForegroundWindow();
            if (!string_begins_with(GetActiveWindowTitle(), avs::game::MODEL)) {
                wnd = FindWindowBeginsWith(avs::game::MODEL);
            }
            if (!wnd) {
                log_warning("jubeat", "could not find window handle for touch");
                TOUCH_ENABLE = false;
                return;
            }

            log_info("jubeat", "using window handle for touch: {}", fmt::ptr(wnd));
            touch_create_wnd(wnd, true);

            // let the rawinput stack correct the aspect ratio
            ::rawinput::touch::ASPECT_COMPENSATION_GAME = true;

            if (GRAPHICS_SHOW_CURSOR) {
                ShowCursor(TRUE);
            }

            // only the L44 model runs in portrait
            if (!avs::game::is_model("L44")) {
                IS_PORTRAIT = false;
            }

            TOUCH_ATTACHED = true;
        }

        // reset state and read the current touch points (device.cpp already
        // compensates for orientation)
        memset(TOUCH_STATE, 0, sizeof(TOUCH_STATE));
        TOUCH_POINTS.clear();
        touch_get_points(TOUCH_POINTS);
        if (TOUCH_ALGORITHM == Legacy) {

            // legacy: evenly divide the play area into a 4x4 grid
            auto offset = IS_PORTRAIT ? 580 : 0;
            for (auto &tp : TOUCH_POINTS) {
                int x = tp.x * 4 / 768;
                int y = (tp.y - offset) * 4 / (1360 - 580);
                int index = y * 4 + x;
                if (index >= 0 && index < 16) {
                    TOUCH_STATE[index] = true;
                }
            }
        } else {

            // accurate registers only a touch inside a button; improved snaps each touch
            // to the single nearest button within reach (so a gap or centre touch still
            // triggers exactly one button); plus marks every button within a larger reach,
            // so an edge or gap touch can trigger several at once (like the mobile game)
            AxisGeometry gx, gy;
            touch_geometry(gx, gy);
            int radius = touch_radius();
            bool multi = (TOUCH_ALGORITHM == Plus);
            for (auto &tp : TOUCH_POINTS) {
                mark_buttons(tp.x, tp.y, gx, gy, radius, multi);
            }
        }
    }

    // true when the rawinput stack sees at least one touchscreen (checked once)
    static bool touchscreen_detected() {
        static const bool detected = rawinput::touch::is_enabled(RI_MGR.get());
        return detected;
    }

    // auto draws the boxes only when a touch screen is present
    static bool debug_show_boxes() {
        switch (TOUCH_DEBUG_OVERLAY) {
            case JbTouchDebugBox:
            case JbTouchDebugAll:
                return true;
            case JbTouchDebugAuto:
                return touchscreen_detected();
            default:
                return false;
        }
    }

    static bool debug_show_taps() {
        return TOUCH_DEBUG_OVERLAY == JbTouchDebugAll;
    }

    bool touch_debug_overlay_enabled() {
        return debug_show_boxes() || debug_show_taps();
    }

    // the 16 boundary rects: legacy divides the area evenly, others use button squares
    static void debug_cells(bool legacy, const AxisGeometry &gx, const AxisGeometry &gy, RECT cells[16]) {
        if (legacy) {
            int offset = IS_PORTRAIT ? 580 : 0;
            int x_edges[5];
            int y_edges[5];
            for (int i = 0; i <= 4; i++) {
                x_edges[i] = i * 768 / 4;
                y_edges[i] = offset + i * (1360 - 580) / 4;
            }
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    cells[r * 4 + c] = { x_edges[c], y_edges[r], x_edges[c + 1], y_edges[r + 1] };
                }
            }
        } else {
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    cells[r * 4 + c] = {
                        gx.button[c], gy.button[r],
                        gx.button[c] + JB_BUTTON_SIZE, gy.button[r] + JB_BUTTON_SIZE
                    };
                }
            }
        }
    }

    // hollow box outlines: grey idle, thick green when pressed (PS_INSIDEFRAME stays inside)
    static void draw_debug_boxes(HDC hdc, bool legacy, const AxisGeometry &gx, const AxisGeometry &gy) {
        RECT cells[16];
        debug_cells(legacy, gx, gy, cells);

        HPEN pen_idle = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
        HPEN pen_active = CreatePen(PS_INSIDEFRAME, 4, RGB(0, 200, 0));
        for (int i = 0; i < 16; i++) {
            HGDIOBJ old_pen = SelectObject(hdc, TOUCH_STATE[i] ? pen_active : pen_idle);
            Rectangle(hdc, cells[i].left, cells[i].top, cells[i].right, cells[i].bottom);
            SelectObject(hdc, old_pen);
        }
        DeleteObject(pen_idle);
        DeleteObject(pen_active);
    }

    // whether a touch at (px, py) presses at least one button: legacy has no gaps, plus
    // reaches across gaps to nearby buttons, and the others only fire inside a button square
    static bool touch_presses_button(int px, int py, const AxisGeometry &gx, const AxisGeometry &gy,
                                     bool legacy, int radius) {
        if (legacy) {
            return true;
        }
        int reach = (TOUCH_ALGORITHM == Plus) ? radius : 0;
        return nearest_button(px, py, gx, gy, reach) >= 0;
    }

    // 90-degree arc on the circle rim facing the centre of button `index`
    static void draw_tap_arc(HDC hdc, int px, int py, int index,
                             const AxisGeometry &gx, const AxisGeometry &gy, int arc_radius) {
        int c = index % 4;
        int r = index / 4;
        double mid = std::atan2((gy.button[r] + JB_BUTTON_SIZE / 2) - py,
                                (gx.button[c] + JB_BUTTON_SIZE / 2) - px);
        const double quarter = 3.14159265358979323846 / 2.0;
        const int segments = 16;
        POINT arc[segments + 1];
        for (int i = 0; i <= segments; i++) {
            double a = mid - quarter / 2.0 + quarter * i / segments;
            arc[i].x = px + (LONG) std::lround(arc_radius * std::cos(a));
            arc[i].y = py + (LONG) std::lround(arc_radius * std::sin(a));
        }
        Polyline(hdc, arc, segments + 1);
    }

    // circle at each touch: white when it presses a button, grey otherwise; in single-button
    // modes a gap touch also gets a white arc facing the button it snapped to (skipped in
    // plus, where a touch can trigger several buttons at once)
    static void draw_debug_taps(HDC hdc, bool legacy, const AxisGeometry &gx, const AxisGeometry &gy) {
        // PS_INSIDEFRAME keeps the stroke inside the circle radius
        const int stroke = 4;
        HPEN pen_white = CreatePen(PS_INSIDEFRAME, stroke, RGB(255, 255, 255));
        HPEN pen_gray = CreatePen(PS_INSIDEFRAME, stroke, RGB(128, 128, 128));
        HGDIOBJ old_pen = SelectObject(hdc, pen_white);

        // accurate has no reach, so fall back to a visible marker size when drawing the circle
        int radius = touch_radius();
        int draw_radius = radius > 0 ? radius : JB_TOUCH_RADIUS;

        for (auto &tp : TOUCH_POINTS) {
            bool pressed = touch_presses_button(tp.x, tp.y, gx, gy, legacy, radius);

            SelectObject(hdc, pressed ? pen_white : pen_gray);
            Ellipse(hdc, tp.x - draw_radius, tp.y - draw_radius,
                         tp.x + draw_radius, tp.y + draw_radius);

            // the arc would point at a single snapped-to button; plus can trigger several at
            // once, so skip it there
            if (pressed || TOUCH_ALGORITHM == Plus) {
                continue;
            }
            int index = nearest_button(tp.x, tp.y, gx, gy, radius);
            if (index >= 0) {
                SelectObject(hdc, pen_white);
                draw_tap_arc(hdc, tp.x, tp.y, index, gx, gy, draw_radius - stroke / 2);
            }
        }

        SelectObject(hdc, old_pen);
        DeleteObject(pen_white);
        DeleteObject(pen_gray);
    }

    void touch_draw_debug_overlay(HDC hdc) {

        // only draw while touch is active
        if (!TOUCH_ENABLE) {
            return;
        }

        bool show_boxes = debug_show_boxes();
        bool show_taps = debug_show_taps();
        if (!show_boxes && !show_taps) {
            return;
        }

        // legacy divides the field evenly; the other algorithms use button squares
        bool legacy = (TOUCH_ALGORITHM == Legacy);
        AxisGeometry gx {}, gy {};
        if (!legacy) {
            touch_geometry(gx, gy);
        }

        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        if (show_boxes) {
            draw_debug_boxes(hdc, legacy, gx, gy);
        }
        if (show_taps) {
            draw_debug_taps(hdc, legacy, gx, gy);
        }
        SelectObject(hdc, old_brush);
    }

    void touch_attach() {
        TOUCH_ENABLE = true;

        switch (TOUCH_ALGORITHM) {
            case Legacy:
                log_info("jubeat", "using 'legacy' touch targets");
                break;
            case Improved:
                log_info("jubeat", "using 'improved' touch targets");
                break;
            case Plus:
                log_info("jubeat", "using 'plus' touch targets");
                break;
            case AcAccurate:
                log_info("jubeat", "using 'ac accurate' touch targets");
                break;
            default:
                log_fatal("jubeat", "unknown touch algo, this is a bug");
                break;
        }
    }

    void touch_detach() {
        TOUCH_ENABLE = false;
    }
}
