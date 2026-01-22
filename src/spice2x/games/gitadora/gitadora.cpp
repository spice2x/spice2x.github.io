#include "gitadora.h"
#include "handle.h"
#include "bi2x_hook.h"
#include <unordered_map>
#include "cfg/configurator.h"
#include "hooks/audio/mme.h"
#include "hooks/graphics/graphics.h"
#include "misc/wintouchemu.h"
#include "overlay/overlay.h"
#include "util/cpuutils.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/sigscan.h"
#include "util/socd_cleaner.h"
#include "hooks/setupapihook.h"

namespace games::gitadora {

    // settings
    bool TWOCHANNEL = false;
    std::optional<unsigned int> CAB_TYPE = std::nullopt;
    bool ARENA_SINGLE_WINDOW = false;
    bool P1_LEFTY = false;
    bool P2_LEFTY = false;
    std::optional<std::string> SUBSCREEN_OVERLAY_SIZE;
    std::optional<socd::SocdAlgorithm> PICK_ALGO = socd::SocdAlgorithm::PreferRecent;

    /*
     * Prevent GitaDora from creating folders on F drive
     */

#ifdef SPICE64
    static DWORD WINAPI GetLogicalDrives_hook() {
        return GetLogicalDrives() | 0x20;
    }

    static UINT WINAPI GetDriveTypeA_hook(LPCSTR lpRootPathName) {
        if (!strcmp(lpRootPathName, "F:\\")) {
            return DRIVE_FIXED;
        }

        return GetDriveTypeA(lpRootPathName);
    }

    static BOOL WINAPI CreateDirectoryA_hook(LPCSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
        if (!strncmp(lpPathName, "F:/", 3)) {
            return TRUE;
        }

        return CreateDirectoryA(lpPathName, lpSecurityAttributes);
    }
#endif

    /*
     * GitaDora checks if the IP address has changed, and if it has it throws 5-1506-0000 like jubeat.
     * We don't want this so we patch it out.
     */
    static char __cdecl eam_network_detected_ip_change() {
        return 0;
    }

    /*
     * GitaDora checks if the server it connects to is in the 192.168.0.0/16 or 169.254.0.0/16 subnet.
     * If it is, it downright refuses to use it and errors with no visible indication.
     * We don't want this so we patch it out.
     */
    static char __cdecl eam_network_settings_conflict() {
        return 0;
    }

    /*
     * Prevent GitaDora from changing the volume setting.
     */
    static long __cdecl bmsd2_set_windows_volume(int volume) {
        return 0;
    }

#ifdef SPICE64

    /*
     * Two Channel Audio Mode
     * We proxy bmsd2_boot_hook and modify the last parameter which is apparently the channel count.
     * Since this apparently isn't the only thing required we need a signature scan to modify a value as well.
     */
    typedef int (__cdecl *bmsd2_boot_t)(long a1, int a2, long a3, char channel_count);
    static bmsd2_boot_t bmsd2_boot_orig = nullptr;
    static int __cdecl bmsd2_boot_hook(long a1, int a2, long a3, char channel_count) {
        return bmsd2_boot_orig(a1, a2, a3, 2);
    }

#endif

    /*
     * Command Line Arguments
     * We hook this to override specific values.
     * This currently disables the ability to specify your own in the app-config.xml (param/cmdline __type="str")
     */
    static bool __cdecl sys_code_get_cmdline(const char *cmdline) {
        if (strcmp(cmdline, "-d") == 0) {
            return true;
        } else if (strcmp(cmdline, "-DM") == 0) {
            return true;
        } else if (strcmp(cmdline, "-WINDOW") == 0) {
            return GRAPHICS_WINDOWED;
        } else if (strcmp(cmdline, "-LOGOUT") == 0) {
            return false;
        } else if (strcmp(cmdline, "-AOU") == 0) {
            return false;
        } else if (strcmp(cmdline, "-QCMODE") == 0) {
            return false;
        } else if (strcmp(cmdline, "-FACTORY") == 0) {
            return false;
        }
        return false;
    }

    /*
     * System Setting Parameter Overrides
     */
    static std::unordered_map<std::string, long> SYS_SETTINGS;
    static std::unordered_map<std::string, long> SYS_DEBUG_DIPS;

    static long __cdecl sys_setting_get_param(const char *param) {

        // overrides
        if (strcmp(param, "PRODUCTION_MODE") == 0) {
            return 0;
        } else if (strcmp(param, "ENABLE_DISP_ID") == 0) {
            return 0;
        } else if (CAB_TYPE.has_value() && strcmp(param, "VER_MACHINE") == 0) {
            return CAB_TYPE.value() << 12;
        }

        // map lookup
        auto it = SYS_SETTINGS.find(param);
        if (it != SYS_SETTINGS.end()) {
            return it->second;
        }

        return -1;
    }

    static long __cdecl sys_setting_set_param(const char *param, long value) {
        SYS_SETTINGS[std::string(param)] = value;

        return 1;
    }

    static long __cdecl sys_debug_dip_get_param(const char *param) {

        // overrides
        if (strcmp(param, "sysinfo") == 0) {
            return 0;
        } else if (strcmp(param, "jobbar1") == 0) {
            return 0;
        } else if (strcmp(param, "jobbar2") == 0) {
            return 0;
        } else if (strcmp(param, "serial") == 0) {
            return 0;
        } else if (strcmp(param, "warnvpf") == 0) {
            return 0;
        } else if (strcmp(param, "scrshot") == 0) {
            return 0;
        } else if (strcmp(param, "eamxml") == 0) {
            return 0;
        } else if (strcmp(param, "offset") == 0) {
            return 0;
        } else if (strcmp(param, "autodbg") == 0) {
            return 0;
        } else if (strcmp(param, "develop") == 0) {
            return 0;
        } else if (strcmp(param, "effect_test") == 0) {
            return 0;
        } else if (strcmp(param, "voice_type2") == 0) {
            return 0;
        }

        // map lookup
        auto it = SYS_DEBUG_DIPS.find(param);
        if (it != SYS_DEBUG_DIPS.end()) {
            return it->second;
        }

        return -1;
    }

    static long __cdecl sys_debug_dip_set_param(const char *param, long value) {
        SYS_DEBUG_DIPS[std::string(param)] = value;

        return 1;
    }

    GitaDoraGame::GitaDoraGame() : Game("GitaDora") {
    }

    void GitaDoraGame::pre_attach() {
        Game::pre_attach();

        if (!cfg::CONFIGURATOR_STANDALONE) {
            if (CAB_TYPE.has_value()) {
                log_info("gitadora", "cab type: {}", CAB_TYPE.value());
            } else {
                log_warning("gitadora", "cab type: not set");
            }

            log_info("gitadora", "applying processor affinity workaround to prevent hangs...");
#ifdef SPICE64
            // workaround for hang on title screen, on systems with many SMT threads
            // exact cause is unknown; most likely a bad assumption in some video decoder
            // 0xFF (first 8 LPs) seems to work well for most people
            cpuutils::set_processor_affinity(0xFF, false);
#else
            // XG versions ran on ancient dual-core AMD systems
            // having more cores cause random hangs on song select screen
            cpuutils::set_processor_affinity(0x3, false);

            // check invalid cab type
            if (CAB_TYPE.has_value() && CAB_TYPE.value() == 3) {
                log_fatal("gitadora", "Cabinet type 3 (SD2) not supported on XG series");
            }
#endif

            // arena model launches a tiny window yet backbuffer at 4k, resulting in unusable overlay
            // force scaling to make things usable
            if (!overlay::UI_SCALE_PERCENT.has_value() && is_arena_model() &&
                GRAPHICS_WINDOWED && !cfg::CONFIGURATOR_STANDALONE) {
                overlay::UI_SCALE_PERCENT = 250;
            }

            // for guitar wail SOCD cleaning
            socd::ALGORITHM = socd::SocdAlgorithm::PreferRecent;

            // for guitar picking
            if (PICK_ALGO.has_value()) {
                log_info("gitadora", "guitar pick SOCD algorithm: {}", static_cast<int>(PICK_ALGO.value()));
            } else {
                log_info("gitadora", "guitar pick SOCD algorithm: legacy");
            }
        }
    }

    void GitaDoraGame::attach() {
        Game::attach();

        // modules
        HMODULE sharepj_module = libutils::try_module("libshare-pj.dll");
        HMODULE bmsd2_module = libutils::try_module("libbmsd2.dll");
        HMODULE system_module = libutils::try_module("libsystem.dll");

        // patches
        detour::inline_hook((void *) eam_network_detected_ip_change, libutils::try_proc(
                sharepj_module, "eam_network_detected_ip_change"));
        detour::inline_hook((void *) eam_network_settings_conflict, libutils::try_proc(
                sharepj_module, "eam_network_settings_conflict"));
        detour::inline_hook((void *) bmsd2_set_windows_volume, libutils::try_proc(
                bmsd2_module, "bmsd2_set_windows_volume"));
        detour::inline_hook((void *) sys_code_get_cmdline, libutils::try_proc(
                system_module, "sys_code_get_cmdline"));
        detour::inline_hook((void *) sys_setting_get_param, libutils::try_proc(
                system_module, "sys_setting_get_param"));
        detour::inline_hook((void *) sys_setting_set_param, libutils::try_proc(
                system_module, "sys_setting_set_param"));
        detour::inline_hook((void *) sys_debug_dip_get_param, libutils::try_proc(
                system_module, "sys_debug_dip_get_param"));
        detour::inline_hook((void *) sys_debug_dip_set_param, libutils::try_proc(
                system_module, "sys_debug_dip_set_param"));

#ifdef SPICE64
        // gitadora arena model 
        auto aio = libutils::try_library("libaio.dll");
        if (aio != nullptr) {
            SETUPAPI_SETTINGS settings{};
            settings.class_guid[0] = 0x86E0D1E0;
            settings.class_guid[1] = 0x11D08089;
            settings.class_guid[2] = 0x0008E49C;
            settings.class_guid[3] = 0x731F303E;
            const char property[] = "1CCF(8050)_000";
            const char property_hardwareid[] = "USB\\VID_1CCF&PID_8050&MI_00\\000";
            memcpy(settings.property_devicedesc, property, sizeof(property));
            memcpy(settings.property_hardwareid, property_hardwareid, sizeof(property_hardwareid));
            setupapihook_init(avs::game::DLL_INSTANCE);
            setupapihook_add(settings);

            // Gitadora IO(J32D/J33I) board emulation
            devicehook_init(avs::game::DLL_INSTANCE);
            devicehook_add(new GitaDoraSerialHandle());

            // test/service/coin buttons
            bi2x_hook_init();

            // f: drive hook
            detour::iat_try("GetLogicalDrives", GetLogicalDrives_hook, avs::game::DLL_INSTANCE);
            detour::iat_try("GetDriveTypeA", GetDriveTypeA_hook, avs::game::DLL_INSTANCE);
            detour::iat_try("CreateDirectoryA", CreateDirectoryA_hook, avs::game::DLL_INSTANCE);

            // volume change prevention
            hooks::audio::mme::init(avs::game::DLL_INSTANCE);

            // touch hook
            if (ARENA_SINGLE_WINDOW) {
                wintouchemu::FORCE = true;
                wintouchemu::INJECT_MOUSE_AS_WM_TOUCH = true;
                wintouchemu::hook("GITADORA", avs::game::DLL_INSTANCE);
            }
            return;
        }

        HMODULE gdme_module = libutils::try_module("libgdme.dll");

        // window patch
        if (GRAPHICS_WINDOWED && !replace_pattern(
                gdme_module,
                "754185ED753D8B4118BF0000CB02",
                "9090????9090??????????????12", 0, 0))
        {
            log_warning("gitadora", "windowed mode failed");
        }

        HMODULE bmsd_engine_module = libutils::try_module("libbmsd-engine.dll");
        HMODULE bmsd_module = libutils::try_module("libbmsd.dll");

        // two channel mod
        if (TWOCHANNEL) {
            bmsd2_boot_orig = detour::iat_try("bmsd2_boot", bmsd2_boot_hook, bmsd_module);

            if (!(replace_pattern(bmsd_engine_module, "33000000488D", "03??????????", 0, 0) ||
                    replace_pattern(bmsd_engine_module, "330000000F10", "03??????????", 0, 0)))
            {
                log_warning("gitadora", "two channel mode failed");
            }
        }

#endif

    }
}
