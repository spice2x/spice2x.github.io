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
