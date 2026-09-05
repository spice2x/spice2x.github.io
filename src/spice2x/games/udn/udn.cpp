// QueryDisplayConfig
#define _WIN32_WINNT 0x0601

#include "udn.h"

#include <algorithm>
#include <format>

#include "acioemu/handle.h"
#include "bi2x_hook.h"
#include "cfg/screen_resize.h"
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
    bool UDN_ENABLE_MIDI = false;

    static acioemu::ACIOHandle *acio_handle = nullptr;
    static const std::wstring port_name = L"COM1";

    static decltype(RegisterRawInputDevices) *RegisterRawInputDevices_orig = nullptr;
    static decltype(EnumDisplaySettingsW) *EnumDisplaySettingsW_orig = nullptr;
    static decltype(QueryDisplayConfig) *QueryDisplayConfig_orig = nullptr;

    // Unity reapplies its cabinet window settings after startup. Use the
    // existing graphics message callback so changes stay on the window thread.
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
        if (!GRAPHICS_WINDOWED || GRAPHICS_HOOKED_WINDOW != window) {
            return 0;
        }

        static bool interactive_resize = false;
        switch (message) {
            case WM_ENTERSIZEMOVE:
                interactive_resize = true;
                break;
            case WM_EXITSIZEMOVE:
                interactive_resize = false;
                break;
            case WM_STYLECHANGING:
                if (w_param == static_cast<WPARAM>(GWL_STYLE)) {
                    auto styles = reinterpret_cast<STYLESTRUCT *>(l_param);
                    styles->styleNew &= ~WS_POPUP;
                    if (cfg::SCREENRESIZE->window_decoration == cfg::WindowDecorationMode::Borderless) {
                        styles->styleNew &= ~WS_OVERLAPPEDWINDOW;
                    } else {
                        styles->styleNew |= WS_OVERLAPPEDWINDOW;
                    }
                }
                break;
            case WM_WINDOWPOSCHANGING:
                if (!interactive_resize && cfg::SCREENRESIZE->enable_window_resize) {
                    auto position = reinterpret_cast<WINDOWPOS *>(l_param);
                    // The shared WM_MOVE/WM_SIZE handlers keep these values in
                    // sync with interactive changes and the overlay settings.
                    if (!(position->flags & SWP_NOMOVE)) {
                        position->x = cfg::SCREENRESIZE->window_offset_x;
                        position->y = cfg::SCREENRESIZE->window_offset_y;
                    }
                    if (!(position->flags & SWP_NOSIZE)) {
                        position->cx = cfg::SCREENRESIZE->client_width +
                            cfg::SCREENRESIZE->window_deco_width;
                        position->cy = cfg::SCREENRESIZE->client_height +
                            cfg::SCREENRESIZE->window_deco_height;
                    }
                }
                break;
        }
        return 0;
    }

    static BOOL WINAPI RegisterRawInputDevices_hook(
            PCRAWINPUTDEVICE devices, UINT device_count, UINT structure_size) {

        if (devices && device_count > 0 && devices[0].hwndTarget == RI_MGR->input_hwnd) {
            return RegisterRawInputDevices_orig(devices, device_count, structure_size);
        }

        SetLastError(0xDEADBEEF);
        return FALSE;
    }

    static BOOL WINAPI EnumDisplaySettingsW_hook(
            LPCWSTR device_name, DWORD mode_number, DEVMODEW *mode) {

        const auto result = EnumDisplaySettingsW_orig(device_name, mode_number, mode);
        if (result && mode_number == ENUM_CURRENT_SETTINGS) {
            mode->dmPelsWidth = 1920;
            mode->dmPelsHeight = 1080;
        }
        return result;
    }

    static LONG WINAPI QueryDisplayConfig_hook(
            UINT32, UINT32 *, DISPLAYCONFIG_PATH_INFO *, UINT32 *, DISPLAYCONFIG_MODE_INFO *,
            DISPLAYCONFIG_TOPOLOGY_ID *) {
        return ERROR_NOT_SUPPORTED;
    }

    void UDNGame::pre_attach() {
        // The title has no MIDI controls. The legacy WinMM provider can fault in
        // MMDevAPI while Unity and its audio backend initialize concurrently, so
        // disable enumeration by default. -udnmidi lets users opt back in for
        // MIDI button bindings.
        rawinput::DISABLE_MIDI = !UDN_ENABLE_MIDI;
        if (rawinput::DISABLE_MIDI) {
            log_info("udn", "MIDI enumeration disabled; use -udnmidi to enable it");
        }
    }

    void UDNGame::attach() {
        Game::attach();

        // Use half of the game's 1920x3252 single-window render surface by default.
        if (GRAPHICS_WINDOWED) {
            if (!GRAPHICS_WINDOW_SIZE.has_value()) {
                GRAPHICS_WINDOW_SIZE = std::make_pair(960u, 1626u);
            }
            if (!GRAPHICS_WINDOW_POS.has_value()) {
                const auto [width, height] = GRAPHICS_WINDOW_SIZE.value();
                const auto x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - (int) width) / 2);
                const auto y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - (int) height) / 2);
                GRAPHICS_WINDOW_POS = std::format("{},{}", x, y);
            }
            if (!GRAPHICS_WINDOW_STYLE.has_value()) {
                GRAPHICS_WINDOW_STYLE = cfg::WindowDecorationMode::ResizableFrame;
            }
            log_info(
                "udn",
                "managed Unity window: client {}x{}, position {}",
                GRAPHICS_WINDOW_SIZE->first,
                GRAPHICS_WINDOW_SIZE->second,
                GRAPHICS_WINDOW_POS.value());
        }

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
        detour::trampoline_try(user32_dll, "QueryDisplayConfig",
                               QueryDisplayConfig_hook, &QueryDisplayConfig_orig);
        detour::trampoline_try(user32_dll, "EnumDisplaySettingsW",
                               EnumDisplaySettingsW_hook, &EnumDisplaySettingsW_orig);
        graphics_add_wnd_proc(window_proc);

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
        graphics_remove_wnd_proc(window_proc);
        devicehook_dispose();
    }
}
