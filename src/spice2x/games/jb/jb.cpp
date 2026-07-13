#include "jb.h"

#include <windows.h>
#include <filesystem>

#include "avs/game.h"
#include "cfg/configurator.h"
#include "hooks/graphics/graphics.h"
#include "launcher/launcher.h"
#include "touch/touch.h"
#include "rawinput/touch.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/detour.h"
#include "util/libutils.h"

// touch layout: a 4x4 grid of 160px buttons separated by 37 / 38 / 37 px gaps
// (752px across). the first button's top-left is at (8, 602) in portrait and
// (6, 8) in landscape.
#define JB_BUTTON_SIZE 160

// improved mode snaps each touch to the nearest button within this reach; a touch
// farther than this from every button registers nothing. must be >= the diagonal
// half of the widest gap (~27px) so the grid centre still reaches a button.
#define JB_TOUCH_RADIUS 30

namespace games::jb {

    // touch state
    JubeatTouchAlgorithm TOUCH_ALGORITHM = Improved;
    bool TOUCH_DEBUG_OVERLAY = false;
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

            // accurate registers only a touch inside a button; improved snaps each
            // touch to the single nearest button within JB_TOUCH_RADIUS (so a gap or
            // centre touch still triggers exactly one button, never two)
            AxisGeometry gx, gy;
            touch_geometry(gx, gy);
            int radius = (TOUCH_ALGORITHM == AcAccurate) ? 0 : JB_TOUCH_RADIUS;
            for (auto &tp : TOUCH_POINTS) {
                int index = nearest_button(tp.x, tp.y, gx, gy, radius);
                if (index >= 0) {
                    TOUCH_STATE[index] = true;
                }
            }
        }
    }

    bool touch_debug_overlay_enabled() {
        return TOUCH_DEBUG_OVERLAY;
    }

    void touch_draw_debug_overlay(HDC hdc) {

        if (!TOUCH_ENABLE) {
            return;
        }

        bool legacy = (TOUCH_ALGORITHM == Legacy);
        AxisGeometry gx, gy;

        // build the 16 cell rectangles for the active algorithm
        RECT cells[16];

        if (legacy) {

            // legacy: even 4x4 division of the play area
            int offset = IS_PORTRAIT ? 580 : 0;
            int x_edges[5];
            int y_edges[5];
            for (int i = 0; i <= 4; i++) {
                x_edges[i] = i * 768 / 4;
                y_edges[i] = offset + i * (1360 - 580) / 4;
            }
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    RECT &cell = cells[r * 4 + c];
                    cell.left = x_edges[c];
                    cell.top = y_edges[r];
                    cell.right = x_edges[c + 1];
                    cell.bottom = y_edges[r + 1];
                }
            }
        } else {

            // both draw the raw button squares (improved's radius applies to the tap)
            touch_geometry(gx, gy);
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    RECT &cell = cells[r * 4 + c];
                    cell.left = gx.button[c];
                    cell.top = gy.button[r];
                    cell.right = cell.left + JB_BUTTON_SIZE;
                    cell.bottom = cell.top + JB_BUTTON_SIZE;
                }
            }
        }

        // hollow outlines so the game stays visible: grey when idle, thicker green
        // when pressed (PS_INSIDEFRAME keeps the green fully inside the cell)
        HPEN pen_idle = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
        HPEN pen_active = CreatePen(PS_INSIDEFRAME, 4, RGB(0, 200, 0));
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

        for (int i = 0; i < 16; i++) {
            HGDIOBJ old_pen = SelectObject(hdc, TOUCH_STATE[i] ? pen_active : pen_idle);
            Rectangle(hdc, cells[i].left, cells[i].top, cells[i].right, cells[i].bottom);
            SelectObject(hdc, old_pen);
        }

        // improved mode only: mark each touch point with a faint thin dotted finger-
        // radius circle plus a line to the button it snapped to. the color key overlay
        // can't do real alpha, but the dotted circle lets the game show through the gaps
        if (TOUCH_ALGORITHM == Improved) {
            HPEN pen_tap = CreatePen(PS_DOT, 1, RGB(0, 200, 255));
            HPEN pen_link = CreatePen(PS_SOLID, 1, RGB(0, 200, 0));
            HGDIOBJ old_tap_pen = SelectObject(hdc, pen_tap);
            for (auto &tp : TOUCH_POINTS) {

                // faint dotted circle at the touch point
                SelectObject(hdc, pen_tap);
                Ellipse(hdc, tp.x - JB_TOUCH_RADIUS, tp.y - JB_TOUCH_RADIUS,
                             tp.x + JB_TOUCH_RADIUS, tp.y + JB_TOUCH_RADIUS);

                // draw a line to the triggered button's centre so it's clear which
                // one the touch snapped to (drawn whether it's inside a button or a gap)
                int index = nearest_button(tp.x, tp.y, gx, gy, JB_TOUCH_RADIUS);
                if (index >= 0) {
                    int c = index % 4;
                    int r = index / 4;
                    SelectObject(hdc, pen_link);
                    MoveToEx(hdc, tp.x, tp.y, nullptr);
                    LineTo(hdc, gx.button[c] + JB_BUTTON_SIZE / 2,
                                gy.button[r] + JB_BUTTON_SIZE / 2);
                }
            }
            SelectObject(hdc, old_tap_pen);
            DeleteObject(pen_tap);
            DeleteObject(pen_link);
        }

        SelectObject(hdc, old_brush);
        DeleteObject(pen_idle);
        DeleteObject(pen_active);
    }

    // fixes "IP ADDR CHANGE" errors with unusual network setups (e.g. a VPN)
    static BOOL __stdcall network_addr_is_changed() {
        return 0;
    }

    // fixes lag spikes from the periodic ping to "eamuse.konami.fun"
    static BOOL __stdcall network_get_network_check_info() {
        return 0;
    }

    // fixes network errors on non-DHCP interfaces
    static BOOL __cdecl network_get_dhcp_result() {
        return 1;
    }

    static int __cdecl GFDbgSetReportFunc(void *func) {
        log_misc("jubeat", "GFDbgSetReportFunc hook hit");

        return 0;
    }

    JBGame::JBGame() : Game("Jubeat") {
    }

    void JBGame::pre_attach() {
        if (!cfg::CONFIGURATOR_STANDALONE) {
            const auto current_path = std::filesystem::current_path();
            log_misc("jubeat", "current working directory: {}", current_path);
            if (current_path.parent_path() == current_path.root_path()) {
                log_warning(
                    "jubeat",
                    "\n\nInvalid path error; jubeat cannot run from a directory in the drive root\n"
                    "The game will overflow the stack and silently fail to boot\n\n"
                    "Instead, it must be at least two levels deep, for example:\n"
                    "    c:\\jubeat\\spice.exe           <- CRASH\n"
                    "    c:\\jubeat\\contents\\spice.exe  <- OK\n\n"
                    "To fix this, create a new directory and move ALL game files there.\n\n"
                    "Your current working directory: {}\n",
                    current_path);

                log_fatal(
                    "jubeat",
                    "Invalid path error; jubeat cannot run from a directory in the drive root");
            }
        }
    }

    void JBGame::attach() {
        Game::attach();

        // enable touch
        TOUCH_ENABLE = true;

        switch (TOUCH_ALGORITHM) {
            case Legacy:
                log_info("jubeat", "using 'legacy' touch targets");
                break;
            case Improved:
                log_info("jubeat", "using 'improved' touch targets");
                break;
            case AcAccurate:
                log_info("jubeat", "using 'ac accurate' touch targets");
                break;
            default:
                log_fatal("jubeat", "unknown touch algo, this is a bug");
                break;
        }

        // enable debug logging of gftools
        HMODULE gftools = libutils::try_module("gftools.dll");
        detour::inline_hook((void *) GFDbgSetReportFunc, libutils::try_proc(
                gftools, "GFDbgSetReportFunc"));

        // apply patches
        HMODULE network = libutils::try_module("network.dll");
        detour::inline_hook((void *) network_addr_is_changed, libutils::try_proc(
                network, "network_addr_is_changed"));
        detour::inline_hook((void *) network_get_network_check_info, libutils::try_proc(
                network, "network_get_network_check_info"));
        detour::inline_hook((void *) network_get_dhcp_result, libutils::try_proc(
                network, "network_get_dhcp_result"));
    }

    void JBGame::detach() {
        Game::detach();

        // disable touch
        TOUCH_ENABLE = false;
    }
}
