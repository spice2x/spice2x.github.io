#include "we.h"

#include "avs/game.h"
#include "hooks/sleephook.h"
#include "hooks/graphics/graphics.h"
#include "hooks/setupapihook.h"
#include "hooks/devicehook.h"
#include "launcher/launcher.h"
#include "util/libutils.h"
#include "util/sigscan.h"
#include "util/utils.h"
#include "util/detour.h"
#include "games/shared/lcdhandle.h"
#include "touchpanel.h"

/*
 * Available Flags
 * PRODUCT_TITLE -> WORLD SOCCER Winning Eleven ARCADE CHAMPIONSHIP 2012
 * IS_FULLSCREEN -> 1
 * SCREEN_W -> 1280
 * SCREEN_H -> 1024
 * WAIT_VSYNC -> 1
 * IS_USE_IO -> 1
 * IS_USE_TOUCH_PANEL -> 1
 * IO_BOARD_COM_PORT -> 2
 * TOUCH_PANEL_COM_PORT -> 1
 * IS_LCD_CONTROLED -> 1
 * TEST_CARDID -> 000000000000000 (15!)
 * IS_USE_AUDIO -> 1
 * IS_USE_AUDIO_EQ -> 1
 * IS_LOCATION_TEST -> 0
 * DISCONN -> 1
 * UNFAIR_CHECK -> 1
 * SCREEN_A -> 0
 * READ_FONT -> 1
 * MEM_DEBUG -> 0
 * MG_TEST -> 0
 * DEBUG_CHAINA_IO -> 0
 * ONLINEUPDATE_QUICK -> 0
 * UDP_COMM_CANCEL -> 0
 * ENABLE_BALL_MOVE -> NONE
 * WE2008AC_PASS_08 -> NONE
 * DISCONN -> NONE
 */
static void set_flag(std::string flag, std::string value) {
    auto module = avs::game::DLL_INSTANCE;
    if (module) {
        char flag_pad[32] {};
        char value_pad[32] {};
        strncpy(flag_pad, flag.data(), sizeof(flag_pad));
        strncpy(value_pad, value.data(), sizeof(value_pad));
        if (replace_pattern(
                module,
                fmt::format("{}{}",
                        bin2hex(flag_pad, sizeof(flag_pad)),
                        std::string(32, '?')
                ),
                fmt::format("{}{}",
                        bin2hex(flag_pad, sizeof(flag_pad)),
                        bin2hex(value_pad, sizeof(value_pad))),
                0, 0)) {
            log_warning("we", "applied flag {}={}", flag, value);
        } else {
            log_warning("we", "couldn't apply flag {}={}", flag, value);
        }
    }
}

static BOOL WINAPI GetCursorPos_hook(LPPOINT lpPoint) {

    // TODO: not sure why this is required for the cursor to be shown
    if (GRAPHICS_SHOW_CURSOR) {
        static auto cursor = ::LoadCursor(NULL, IDC_ARROW);
        ::SetCursor(cursor);
    }

    // get cursor position
    auto result = GetCursorPos(lpPoint);
    if (result) {

        // try to find game window - the second window without title
        static HWND target = 0;
        if (target == 0) {
            bool skip = false;
            for (auto &hWnd : GRAPHICS_WINDOWS) {
                char buffer[2] {};
                auto len = GetWindowTextA(hWnd, buffer, sizeof(buffer));
                if (len == 0) {
                    if (skip) {
                        target = hWnd;
                        break;
                    } else {
                        skip = true;
                    }
                }
            }
        }

        // translate screen coordinates to game window
        if (target) {
            ScreenToClient(target, lpPoint);
        }
    }

    // return original result
    return result;
}

games::we::WEGame::WEGame() : Game("Winning Eleven") {
}

void games::we::WEGame::attach() {
    Game::attach();

    // touchscreen device must just be present
    SETUPAPI_SETTINGS touch_device {};
    std::string touch_device_desc = "Elo TouchSystems 2700 IntelliTouch USB Touchmonitor Interface";
    memcpy(touch_device.property_devicedesc, touch_device_desc.c_str(), touch_device_desc.length() + 1);
    setupapihook_init(avs::game::DLL_INSTANCE);
    setupapihook_add(touch_device);

    // fix cursor position
    detour::iat_try("GetCursorPos", GetCursorPos_hook);

    // hooks for other touchscreen
    touchpanel_init();

    // make game not stretch window to main screen
    if (GRAPHICS_WINDOWED) {
        set_flag("IS_FULLSCREEN", "0");
    }

    // LCD handle
    devicehook_init();
    devicehook_add(new games::shared::LCDHandle());

    // Winning Eleven 2014 sleeps for 60 seconds in gameMain
    auto weac = libutils::try_library(MODULE_PATH / "weac.dll");
    if (weac != nullptr) {
        hooks::sleep::init(59999, 1, weac);
    }
}

void games::we::WEGame::detach() {
    Game::detach();
}
