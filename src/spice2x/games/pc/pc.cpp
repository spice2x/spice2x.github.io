#include "pc.h"

#include <format>

#include "bi2x_hook.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/fileutils.h"
#include "util/unity_player.h"
#include "util/execexe.h"
#include "acioemu/handle.h"
#include "misc/wintouchemu.h"
#include "hooks/graphics/graphics.h"
#include "rawinput/rawinput.h"

namespace games::pc {
    std::string PC_INJECT_ARGS = "";
    bool PC_NO_IO = false;

    static acioemu::ACIOHandle *acioHandle = nullptr;
    static std::wstring portName = L"COM1";

    static decltype(RegisterRawInputDevices) *RegisterRawInputDevices_orig = nullptr;
    static decltype(EnumDisplaySettingsW) *EnumDisplaySettingsW_orig = nullptr;
    static decltype(QueryDisplayConfig) *QueryDisplayConfig_orig = nullptr;

    static BOOL WINAPI RegisterRawInputDevices_hook(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize) {

        // if the caller is spice itself, then pass through.
        if (pRawInputDevices &&
            (uiNumDevices > 0) &&
            (pRawInputDevices[0].hwndTarget == RI_MGR->input_hwnd)) {
            
            return RegisterRawInputDevices_orig(pRawInputDevices, uiNumDevices, cbSize);
        }
        
        // otherwise, it must be the game; prevent the game from registering for raw input
        // and hijacking WM_INPUT messages.
        SetLastError(0xDEADBEEF);
        return FALSE;
    }

    static BOOL WINAPI EnumDisplaySettingsW_hook(LPCWSTR lpszDeviceName, DWORD iModeNum, DEVMODEW* lpDevMode) {
        auto result = EnumDisplaySettingsW_orig(lpszDeviceName, iModeNum, lpDevMode);

        // https://docs.unity3d.com/2022.3/Documentation/ScriptReference/Screen-currentResolution.html
        // "If the player is running in windowed mode, this returns the current dimensions of the game window in pixels and the display refresh rate."
        // Override it to 1920x1080 to avoid broken UI under Windowed mode
        if (result && iModeNum == ENUM_CURRENT_SETTINGS) {
            // Actually make it returns size of the game window, Can I?
            lpDevMode->dmPelsWidth = 1920;
            lpDevMode->dmPelsHeight = 1080;
        }

        return result;
    }

    LONG WINAPI QueryDisplayConfig_hook(UINT32, UINT32*, DISPLAYCONFIG_PATH_INFO*, UINT32*, DISPLAYCONFIG_MODE_INFO*, DISPLAYCONFIG_TOPOLOGY_ID*) {
        // make unity fallback to EnumDisplaySettingsW as I don't 
        // want to deal with this api which is way more complex
        return ERROR_NOT_SUPPORTED;
    }

    void PCGame::attach() {
        Game::attach();

        // add card reader
        acioHandle = new acioemu::ACIOHandle(portName.c_str(), 1);
        devicehook_init_trampoline();
        devicehook_add(acioHandle);

        execexe::init();
        execexe::init_deferred([] () {
            execexe::init_port_hook(portName, acioHandle);
            execexe::load_library("libaio.dll");
            execexe::load_library("libaio-iob.dll");
            execexe::load_library("libaio-iob_video.dll");
            execexe::load_library("libaio-iob2_video.dll");
            execexe::load_library("win10actlog.dll");

            if (!PC_NO_IO) {
                bi2x_hook_init();
            }
        });

        const auto user32Dll = "user32.dll";
        detour::trampoline_try(user32Dll, "RegisterRawInputDevices",
                               RegisterRawInputDevices_hook, &RegisterRawInputDevices_orig);
        detour::trampoline_try(user32Dll, "QueryDisplayConfig", 
                               QueryDisplayConfig_hook, &QueryDisplayConfig_orig);
        detour::trampoline_try(user32Dll, "EnumDisplaySettingsW", 
                               EnumDisplaySettingsW_hook, &EnumDisplaySettingsW_orig);

        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        unity_utils::set_args(
                std::format("{} {}{}",
                            GetCommandLineA(),
                            PC_INJECT_ARGS,
                            unity_utils::get_unity_player_args()));
    }

    void PCGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}
