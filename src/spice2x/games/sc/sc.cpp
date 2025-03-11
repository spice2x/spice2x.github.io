#include "sc.h"

#include "avs/game.h"
#include "hooks/devicehook.h"
#include "hooks/graphics/graphics.h"
#include "hooks/lang.h"
#include "hooks/libraryhook.h"
#include "games/shared/lcdhandle.h"
#include "games/shared/twtouch.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/utils.h"
#include "launcher/shutdown.h"

/**
 * Overridden touchscreen for attaching the touch hooks to the window.
 */
class SCTouchDevice : public games::shared::TwTouchDevice {
public:

    SCTouchDevice() {

        // match factory calibration setting
        this->offset_x = -1500;
        this->offset_y = 3100;
        this->scale_x = (65535.f + 2500) / 1920.f;
        this->scale_y = (65535.f - 5800) / 1080.f;
        this->flip_x = true;
        this->flip_y = true;
    }

    bool open(LPCWSTR lpFileName) override {

        // check if device was opened
        auto result = TwTouchDevice::open(lpFileName);
        if (result) {

            // attach touch module
            HWND hWnd = GetForegroundWindow();
            if (!string_begins_with(GetActiveWindowTitle(), "Graphics")) {
                hWnd = FindWindowBeginsWith("Graphics");
            }

            // check window
            if (hWnd != nullptr) {

                // check if windowed
                if (GRAPHICS_WINDOWED) {

                    // remove style borders
                    LONG lStyle = GetWindowLong(hWnd, GWL_STYLE);
                    lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
                    SetWindowLongPtr(hWnd, GWL_STYLE, lStyle);

                    // remove ex style borders
                    LONG lExStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
                    lExStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
                    SetWindowLongPtr(hWnd, GWL_EXSTYLE, lExStyle);

                    // update/move window (Steel Chronicle runs at 1920x1080)
                    SetWindowPos(hWnd, nullptr, 0, 0, 1920, 1080,
                                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
                } else {

                    // show game window because it lost focus
                    ShowWindow(hWnd, SW_SHOW);
                }
            }

            // use DirectX window for touch events, otherwise game window loses focus and runs at 10 FPS
            touch_attach_dx_hook();

            // cursor
            if (!is_touch_available()) {
                ShowCursor(true);
            }
        }

        // return result
        return result;
    }
};

static int __cdecl setvolume_stub(const char *set_volume_file) {
    return 0;
}

static decltype(GetFileAttributesA)* GetFileAttributesA_orig;

static DWORD __stdcall GetFileAttributesA_hook(LPCSTR lpFileName) {
    
    if (strcmp(lpFileName, "C:\\WERUNTIME.INI") == 0) {
        return INVALID_FILE_ATTRIBUTES;
    }

    return GetFileAttributesA_orig(lpFileName);
}

games::sc::SCGame::SCGame() : Game("Steel Chronicle") {
}

void games::sc::SCGame::attach() {
    Game::attach();

    /*
     * Since there are still some problems when the locale is non-japanese,
     * we want to warn the user about it.
     */
    if (!hooks::lang::is_native_shiftjis()) {
        switch (MessageBoxA(
                NULL,
                "System locale for non-unicode programs not set to Japanese.\n"
                "This is unsupported and the game will crash at some point.\n"
                "Google: \"windows japanese non-unicode\"",
                "Warning",
                MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1))
        {
            case IDCANCEL:
                launcher::kill();
                break;
            case IDOK:
            default:
                break;
        }
    }

    // add devices
    devicehook_init();
    devicehook_add(new games::shared::LCDHandle());
    devicehook_add(new SCTouchDevice());

    // game code resides in another DLL
    auto gamekgg = libutils::load_library("gamekgg.dll");
    devicehook_init(gamekgg);
    libraryhook_enable(gamekgg);

    // set volume hook - default function causes access violation on some setups
    auto setvolume_module = libutils::load_library("setvolume.dll");
    detour::inline_hook((void *) setvolume_stub, libutils::get_proc(setvolume_module, "?setvolume@@YAHPAD@Z"));

    // game changes power configurations if C:\WERUNTIME.INI exists
    GetFileAttributesA_orig = detour::iat_try("GetFileAttributesA", GetFileAttributesA_hook, avs::game::DLL_INSTANCE);
}

void games::sc::SCGame::detach() {
    Game::detach();
    devicehook_dispose();
}
