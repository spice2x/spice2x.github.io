#include "udn.h"

#include <format>

#include "acioemu/handle.h"
#include "bi2x_hook.h"
#include "hooks/devicehook.h"
#include "hooks/graphics/graphics.h"
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/execexe.h"
#include "util/logging.h"
#include "util/unity_player.h"
#include "util/utils.h"

namespace games::udn {
    std::string UDN_INJECT_ARGS = "";
    bool UDN_NO_IO = false;

    static acioemu::ACIOHandle *acio_handle = nullptr;
    static const std::wstring port_name = L"COM1";

    static decltype(RegisterRawInputDevices) *RegisterRawInputDevices_orig = nullptr;

    static BOOL WINAPI RegisterRawInputDevices_hook(
            PCRAWINPUTDEVICE devices, UINT device_count, UINT structure_size) {

        if (devices && device_count > 0 && devices[0].hwndTarget == RI_MGR->input_hwnd) {
            return RegisterRawInputDevices_orig(devices, device_count, structure_size);
        }

        SetLastError(0xDEADBEEF);
        return FALSE;
    }

    void UDNGame::pre_attach() {
        // The title has no MIDI controls. The legacy WinMM provider can fault in
        // MMDevAPI while Unity and its audio backend initialize concurrently, so
        // disable MIDI enumeration for this game.
        rawinput::DISABLE_MIDI = true;
        log_info("udn", "MIDI input disabled to avoid WinMM initialization errors");
    }

    void UDNGame::attach() {
        Game::attach();

        acio_handle = new acioemu::ACIOHandle(port_name.c_str(), 1);
        devicehook_init_trampoline();
        devicehook_add(acio_handle);

        execexe::init();
        execexe::init_deferred([]() {
            execexe::init_port_hook(port_name, acio_handle);

            // Runtime import names omit the "lib" prefix.
            execexe::load_library("aio.dll");
            execexe::load_library("aio-iob.dll");
            execexe::load_library("aio-iob_video.dll");
            execexe::load_library("aio-iob2_video.dll");
            execexe::load_library("win10actlog.dll", false);

#if SPICE64
            if (!UDN_NO_IO) {
                bi2x_hook_init();
            }
#endif
        });

        const auto user32_dll = "user32.dll";
        detour::trampoline_try(user32_dll, "RegisterRawInputDevices",
                               RegisterRawInputDevices_hook, &RegisterRawInputDevices_orig);

        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        unity_utils::set_args(
                std::format("{} {}{}",
                            GetCommandLineA(),
                            UDN_INJECT_ARGS,
                            unity_utils::get_unity_player_args()));
    }

    void UDNGame::detach() {
        Game::detach();
        devicehook_dispose();
    }
}
