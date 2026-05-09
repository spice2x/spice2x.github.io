#include "jb.h"

#include <windows.h>
#include <filesystem>

#include "avs/game.h"
#include "cfg/configurator.h"
#include "hooks/graphics/graphics.h"
#include "touch/touch.h"
#include "util/logging.h"
#include "util/utils.h"
#include "util/detour.h"
#include "util/libutils.h"

#define JB_BUTTON_SIZE 160
#define JB_BUTTON_GAP 37
#define JB_BUTTON_HITBOX (JB_BUTTON_SIZE + JB_BUTTON_GAP)

namespace games::jb {

    // touch stuff
    JubeatTouchAlgorithm TOUCH_ALGORIHTM = Improved;
    static bool TOUCH_ENABLE = false;
    static bool TOUCH_ATTACHED = false;
    static bool IS_PORTRAIT = true;
    static std::vector<TouchPoint> TOUCH_POINTS;
    bool TOUCH_STATE[16];

    void touch_update() {

        // check if touch enabled
        if (!TOUCH_ENABLE) {
            return;
        }

        // attach touch module
        if (!TOUCH_ATTACHED) {

            /*
             * Find the game window.
             * We check the foreground window first, then fall back to searching for the window title
             * All game versions seem to have their model first in the window title
             */
            HWND wnd = GetForegroundWindow();
            if (!string_begins_with(GetActiveWindowTitle(), avs::game::MODEL)) {
                wnd = FindWindowBeginsWith(avs::game::MODEL);
            }

            // check if we have a window handle
            if (!wnd) {
                log_warning("jubeat", "could not find window handle for touch");
                TOUCH_ENABLE = false;
                return;
            }

            // attach touch hook
            log_info("jubeat", "using window handle for touch: {}", fmt::ptr(wnd));
            touch_create_wnd(wnd, true);

            // show cursor
            if (GRAPHICS_SHOW_CURSOR) {
                ShowCursor(TRUE);
            }

            // earlier games use a different screen orientation
            if (!avs::game::is_model("L44")) {
                IS_PORTRAIT = false;
            }

            // set attached
            TOUCH_ATTACHED = true;
        }

        // reset touch state
        memset(TOUCH_STATE, 0, sizeof(TOUCH_STATE));

        // check touch points
        // note that the IO code in device.cpp will correctly compensate for orientation, depending on the model.
        TOUCH_POINTS.clear();
        touch_get_points(TOUCH_POINTS);
        if (TOUCH_ALGORIHTM == Legacy) {
            auto offset = IS_PORTRAIT ? 580 : 0;
            for (auto &tp : TOUCH_POINTS) {

                // get grid coordinates
                int x = tp.x * 4 / 768;
                int y = (tp.y - offset) * 4 / (1360 - 580);

                // set the corresponding state
                int index = y * 4 + x;
                if (index >= 0 && index < 16) {
                    TOUCH_STATE[index] = true;
                }
            }
        } else {
            for (auto &tp : TOUCH_POINTS) {
                int x_relative = tp.x;
                int y_relative = tp.y;

                // x_relative and y_relative are relative to the top-left pixel of the first button
                if (IS_PORTRAIT) {
                    // which is at (8, 602) in portrait:
                    //   X: [8...759] (752 pixels wide)
                    //   Y: [602...1353] (752 pixels high)
                    x_relative -= 8;
                    y_relative -= 602;
                } else {
                    // and at (8, 8) in landscape
                    x_relative -= 8;
                    y_relative -= 8;
                }

                int x_index = -1;
                int x_hitbox = 0;
                int y_index = -1;
                int y_hitbox = 0;

                // x_hitbox and y_hitbox is relative to top-left pixel of each button
                if (x_relative >= 0) {
                    x_index = x_relative / JB_BUTTON_HITBOX;
                    x_hitbox = x_relative % JB_BUTTON_HITBOX;
                }
                if (y_relative >= 0) {
                    y_index = y_relative / JB_BUTTON_HITBOX;
                    y_hitbox = y_relative % JB_BUTTON_HITBOX;
                }

                if (TOUCH_ALGORIHTM == Improved) {
                    if (IS_PORTRAIT) {
                        // extend to left border
                        if (x_relative < 0) {
                            x_index = 0;
                        }
                        // right and bottom borders are covered by the hit box
                    } else {
                        // extend to top border
                        if (y_relative < 0) {
                            y_index = 0;
                        }
                        // extend to left border
                        if (x_relative < 0) {
                            x_index = 0;
                        }
                        // bottom border is covered by the hit box
                        // rightmost edge - ignore them entirely
                        if (x_index == 3 && JB_BUTTON_SIZE < x_hitbox) {
                            continue;
                        }
                    }
                }

                if (x_index < 0 || y_index < 0 || x_index > 3 || y_index > 3) {
                    continue;
                }

                // check if the gap was touched
                if (TOUCH_ALGORIHTM == AcAccurate) {
                    // in ac-accurate mode, touching the gap is ignored
                    if (x_hitbox > JB_BUTTON_SIZE || y_hitbox > JB_BUTTON_SIZE) {
                        continue;
                    }

                } else if (TOUCH_ALGORIHTM == Improved) {
                    // in improved mode, touching the gap triggers the closest button
                    if (x_index <= 2 && (JB_BUTTON_SIZE + JB_BUTTON_GAP / 2) < x_hitbox) {
                        x_index++;
                    }
                    if (y_index <= 2 && (JB_BUTTON_SIZE + JB_BUTTON_GAP / 2) < y_hitbox) {
                        y_index++;
                    }
                }

                // set the corresponding state
                int index = y_index * 4 + x_index;
                if (0 <= index && index < 16) {
                    TOUCH_STATE[index] = true;
                }
            }
        }
    }

    /*
     * to fix "IP ADDR CHANGE" errors on boot and in-game when using weird network setups such as a VPN
     */
    static BOOL __stdcall network_addr_is_changed() {
        return 0;
    }

    /*
     * to fix lag spikes when game tries to ping "eamuse.konami.fun" every few minutes
     */
    static BOOL __stdcall network_get_network_check_info() {
        return 0;
    }

    /*
     * to fix network error on non DHCP interfaces
     */
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

        switch (TOUCH_ALGORIHTM) {
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
                log_fatal("jubeat", "unknown touch algo detected in touch_update, this is a bug");
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
