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
#include "util/deferlog.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/sigscan.h"
#include "util/socd_cleaner.h"
#include "util/sysutils.h"
#include "util/utils.h"
#include "hooks/setupapihook.h"

namespace games::gitadora {

    // settings
    bool TWOCHANNEL = false;
    std::optional<unsigned int> CAB_TYPE = std::nullopt;
    bool P1_LEFTY = false;
    bool P2_LEFTY = false;
    std::optional<std::string> SUBSCREEN_OVERLAY_SIZE;
    std::optional<socd::SocdAlgorithm> PICK_ALGO = socd::SocdAlgorithm::PreferRecent;
    uint8_t ARENA_WINDOW_COUNT = 4;

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
            if (!overlay::UI_SCALE_PERCENT.has_value() && is_arena_model() && !cfg::CONFIGURATOR_STANDALONE) {
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

#if SPICE64 && !SPICE_XP

            if (is_arena_model()) {
                // in full screen, if single-adapter option is checked, it's functionally
                // the same as forcing a single monitor
                if (!GRAPHICS_WINDOWED && GRAPHICS_FORCE_SINGLE_ADAPTER) {
                    ARENA_WINDOW_COUNT = 1;
                }

                // for convenience, in full screen, if we know that the system has fewer than
                // four monitors, downgrade to one monitor
                if (!GRAPHICS_WINDOWED && ARENA_WINDOW_COUNT > 1) {
                    const auto &monitors = sysutils::enumerate_monitors();
                    const size_t active_count = monitors.size();
                    log_info("gitadora", "arena model: detected {} active monitor(s)", active_count);
                    if (active_count < 4) {
                        log_info("gitadora", "arena model: enable single monitor mode due to insufficient monitors");
                        ARENA_WINDOW_COUNT = 1;
                    }
                }
                
                // 2 monitors in full screen is currently not supported
                if (!GRAPHICS_WINDOWED && ARENA_WINDOW_COUNT == 2 && !cfg::CONFIGURATOR_STANDALONE) {
                    log_fatal("gitadora", "arena model: 2 monitors in full screen is currently not supported, choose 1 or 4");
                }

                if (ARENA_WINDOW_COUNT == 1) {
                    log_info("gitadora", "arena model: single window mode");
                    GRAPHICS_FORCE_SINGLE_ADAPTER = true;
                    GRAPHICS_PREVENT_SECONDARY_WINDOWS = true;
                } else if (ARENA_WINDOW_COUNT == 2) {
                    log_info("gitadora", "arena model: two window mode");
                    GRAPHICS_GITADORA_HIDE_SIDE_WINDOWS = true;
                } else if (ARENA_WINDOW_COUNT == 4) {
                    log_info("gitadora", "arena model: four window mode");
                } else if (!cfg::CONFIGURATOR_STANDALONE) {
                    log_fatal("gitadora", "arena model: unsupported window count: {}", ARENA_WINDOW_COUNT);
                }
            }

#endif

        }
    }

#if SPICE64 && !SPICE_XP

    static decltype(GetDisplayConfigBufferSizes) *GetDisplayConfigBufferSizes_orig = nullptr;
    static decltype(QueryDisplayConfig) *QueryDisplayConfig_orig = nullptr;
    static decltype(DisplayConfigGetDeviceInfo) *DisplayConfigGetDeviceInfo_orig = nullptr;

    // cached primary real monitor: its path + source/target mode entries.
    // modeInfoIdx values on the path are renumbered to 0 / 1 so the cache is self-contained.
    static DISPLAYCONFIG_PATH_INFO real_primary_path = {};
    static DISPLAYCONFIG_MODE_INFO real_primary_modes[2] = {}; // [0]=source, [1]=target

    // fake monitors appended after the real ones. the game classifies monitors
    // by outputTechnology + connectorInstance:
    //   HDMI               -> main 4k monitor (real primary)
    //   DP connInstance 0  -> left
    //   DP connInstance 1  -> right
    //   DP connInstance 2  -> small (sub/touch)
    // ids are negated on the fake monitor headers so they can be distinguished
    // from real ones in DisplayConfigGetDeviceInfo.
    struct FakeMonitor {
        LONG id;
        int width;
        int height;
        int offset_x;
        int offset_y;
        UINT32 connector_instance;
    };

    // ORDERING MATTERS: the d3d9 wrapper (FAKE_SUBSCREEN_ADAPTER) hands out
    // adapters as "\\.\DISPLAY_SPICE_FAKE_{N}" for N=1,2,3. The game maps each
    // adapter to a swap chain role via its DisplayConfig connector instance,
    // so the entries here must be listed in the same order the wrapper enumerates
    // them: id=1 -> adapter 1 -> left, id=2 -> adapter 2 -> right, id=3 -> adapter 3 -> small.
    static constexpr FakeMonitor FAKE_MONITORS[] = {
        { 1, 1080, 1920, -100000, -100000, 0 }, // left  (DP connector instance 0)
        { 2, 1080, 1920, -200000, -200000, 1 }, // right (DP connector instance 1)
        { 3,  800, 1280, -300000, -300000, 2 }, // small (DP connector instance 2, touch)
    };
    static constexpr UINT32 FAKE_MONITOR_COUNT = static_cast<UINT32>(std::size(FAKE_MONITORS));

    // call QueryDisplayConfig once, keep only the primary monitor's path and its
    // two referenced modes (source + target). modeInfoIdx values are rewritten to
    // 0 and 1 so the cache is self-consistent.
    static void cache_primary_monitor_info() {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        if (GetDisplayConfigBufferSizes_orig(
                QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
            log_fatal("gitadora", "cache_primary_monitor_info: GetDisplayConfigBufferSizes failed");
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> all_paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> all_modes(mode_count);
        if (QueryDisplayConfig_orig(
                QDC_ONLY_ACTIVE_PATHS,
                &path_count, all_paths.data(),
                &mode_count, all_modes.data(),
                nullptr) != ERROR_SUCCESS) {
            log_fatal("gitadora", "cache_primary_monitor_info: QueryDisplayConfig failed");
        }
        all_paths.resize(path_count);
        all_modes.resize(mode_count);

        // pick the primary monitor: source mode at (0, 0)
        auto primary = std::find_if(all_paths.begin(), all_paths.end(),
            [&](const auto &p) {
                const auto idx = p.sourceInfo.modeInfoIdx;
                return idx < all_modes.size() &&
                       all_modes[idx].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
                       all_modes[idx].sourceMode.position.x == 0 &&
                       all_modes[idx].sourceMode.position.y == 0;
            });
        if (primary == all_paths.end()) {
            log_fatal("gitadora", "cache_primary_monitor_info: no primary monitor found");
        }

        real_primary_modes[0] = all_modes[primary->sourceInfo.modeInfoIdx];
        real_primary_modes[1] = all_modes[primary->targetInfo.modeInfoIdx];
        real_primary_path = *primary;
        real_primary_path.sourceInfo.modeInfoIdx = 0;
        real_primary_path.targetInfo.modeInfoIdx = 1;

        log_info("gitadora", "cache_primary_monitor_info: cached primary monitor");
    }

    static
    LONG
    WINAPI
    GetDisplayConfigBufferSizes_hook(
        UINT32 Flags,
        UINT32 *pNumPathArrayElements,
        UINT32 *pNumModeInfoArrayElements)
    {
        // populate cached primary real monitor on the first call
        static std::once_flag populate_once;
        std::call_once(populate_once, cache_primary_monitor_info);

        // always report exactly 1 real + N fake monitors
        *pNumPathArrayElements = 1 + FAKE_MONITOR_COUNT;
        *pNumModeInfoArrayElements = 2 + FAKE_MONITOR_COUNT * 2;

        log_info(
            "gitadora",
            "GetDisplayConfigBufferSizes: 1 real path + {} fake monitor(s)",
            FAKE_MONITOR_COUNT);

        return ERROR_SUCCESS;
    }

    // write fake monitor i into the caller's path/mode arrays. layout (single-monitor
    // assumption): index 0 in both arrays holds the cached primary real monitor, so
    // fake i occupies path slot (1 + i) and mode slots (2 + i*2) / (2 + i*2 + 1).
    static void insert_fake_monitor(
        DISPLAYCONFIG_PATH_INFO *paths,
        DISPLAYCONFIG_MODE_INFO *modes,
        UINT32 i)
    {
        const FakeMonitor &m = FAKE_MONITORS[i];
        const UINT32 src_idx = 2 + i * 2;
        const UINT32 tgt_idx = src_idx + 1;
        const LUID adapter_id { .LowPart = static_cast<DWORD>(-m.id), .HighPart = -m.id };
        const UINT32 uid = static_cast<UINT32>(-m.id);

        paths[1 + i] = {
            .sourceInfo = {
                .adapterId = adapter_id,
                .id = uid,
                .modeInfoIdx = src_idx,
                .statusFlags = DISPLAYCONFIG_SOURCE_IN_USE,
            },
            .targetInfo = {
                .adapterId = adapter_id,
                .id = uid,
                .modeInfoIdx = tgt_idx,
                .outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL,
                .rotation = DISPLAYCONFIG_ROTATION_IDENTITY,
                .scaling = DISPLAYCONFIG_SCALING_IDENTITY,
                .refreshRate = { .Numerator = 60000, .Denominator = 1000 },
                .scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE,
                .targetAvailable = TRUE,
                .statusFlags = DISPLAYCONFIG_TARGET_IN_USE,
            },
            .flags = DISPLAYCONFIG_PATH_ACTIVE,
        };

        modes[src_idx] = {
            .infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE,
            .id = uid,
            .adapterId = adapter_id,
            .sourceMode = {
                .width = static_cast<UINT32>(m.width),
                .height = static_cast<UINT32>(m.height),
                .pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP,
                .position = { .x = m.offset_x, .y = m.offset_y },
            },
        };

        modes[tgt_idx] = {
            .infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET,
            .id = uid,
            .adapterId = adapter_id,
            .targetMode = {},
        };

        log_misc(
            "gitadora",
            "inserted fake monitor: id={}, width={}, height={}, offset_x={}, offset_y={}",
            m.id, m.width, m.height, m.offset_x, m.offset_y);
    }

    static
    LONG
    WINAPI
    QueryDisplayConfig_hook(
        UINT32 flags,
        UINT32* numPathArrayElements,
        DISPLAYCONFIG_PATH_INFO* pathArray,
        UINT32* numModeInfoArrayElements,
        DISPLAYCONFIG_MODE_INFO* modeInfoArray,
        DISPLAYCONFIG_TOPOLOGY_ID* currentTopologyId)
    {
        // copy cached primary real monitor into caller buffers at index 0
        pathArray[0] = real_primary_path;
        modeInfoArray[0] = real_primary_modes[0];
        modeInfoArray[1] = real_primary_modes[1];
        *numPathArrayElements = 1 + FAKE_MONITOR_COUNT;
        *numModeInfoArrayElements = 2 + FAKE_MONITOR_COUNT * 2;
        if (currentTopologyId != nullptr) {
            *currentTopologyId = DISPLAYCONFIG_TOPOLOGY_EXTEND;
        }

        log_misc("gitadora", "QueryDisplayConfig returning fake monitor paths and modes");

        // append fake monitors after the real one
        for (UINT32 i = 0; i < FAKE_MONITOR_COUNT; i++) {
            insert_fake_monitor(pathArray, modeInfoArray, i);
        }

        return ERROR_SUCCESS;
    }

    static
    LONG
    WINAPI
    DisplayConfigGetDeviceInfo_hook(DISPLAYCONFIG_DEVICE_INFO_HEADER* requestPacket)
    {
        if (requestPacket == nullptr) {
            return DisplayConfigGetDeviceInfo_orig(requestPacket);
        }

        // handle fake monitors (negative id) directly without calling orig
        const auto id = static_cast<int>(requestPacket->id);
        if (id < 0) {
            if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME) {
                const auto sourceName = reinterpret_cast<DISPLAYCONFIG_SOURCE_DEVICE_NAME*>(requestPacket);
                // name must match WrappedIDirect3D9::GetAdapterIdentifier
                const std::string adapter_name = fmt::format("\\\\.\\DISPLAY_SPICE_FAKE_{}", -id);
                wcscpy(sourceName->viewGdiDeviceName, s2ws(adapter_name).c_str());
                log_misc("gitadora",
                    "DisplayConfigGetDeviceInfo: fake source id={} name={}", id, adapter_name);
                return ERROR_SUCCESS;
            }
            if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
                const auto targetName = reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(requestPacket);
                const LONG fake_id = -id;
                UINT32 conn_inst = 0xff;
                for (const auto& f : FAKE_MONITORS) {
                    if (f.id == fake_id) {
                        conn_inst = f.connector_instance;
                        break;
                    }
                }
                targetName->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
                targetName->connectorInstance = conn_inst;
                wcscpy(targetName->monitorFriendlyDeviceName, L"Spice Fake Monitor");
                wcscpy(targetName->monitorDevicePath, L"\\\\?\\SpiceFakeMonitor");
                log_misc("gitadora",
                    "DisplayConfigGetDeviceInfo: fake target id={} -> DP connInst {}",
                    id, targetName->connectorInstance);
                return ERROR_SUCCESS;
            }
        }

        const auto ret = DisplayConfigGetDeviceInfo_orig(requestPacket);
        if (ret != ERROR_SUCCESS ||
            requestPacket->type != DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
            return ret;
        }

        // override the cached primary real monitor target info to look like HDMI/0
        const auto targetName = reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(requestPacket);
        const auto& target = real_primary_path.targetInfo;
        if (target.id == targetName->header.id &&
            target.adapterId.HighPart == targetName->header.adapterId.HighPart &&
            target.adapterId.LowPart == targetName->header.adapterId.LowPart)
        {
            targetName->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
            targetName->connectorInstance = 0;
            log_info("gitadora",
                "overriding primary monitor (id={}) to pretend to be HDMI",
                targetName->header.id);
        }
        return ret;
    }

#endif

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
            
            // monitor/touch hooks (windowed or full screen)
            if (GRAPHICS_PREVENT_SECONDARY_WINDOWS) {
                // enable touch hook for subscreen overlay
                wintouchemu::FORCE = true;
                wintouchemu::INJECT_MOUSE_AS_WM_TOUCH = true;
                wintouchemu::hook("GITADORA", avs::game::DLL_INSTANCE);

#if !SPICE_XP

                if (!GRAPHICS_WINDOWED) {
                    // monitor hook: always pretend to have 1 primary real monitor + 3 fake monitors
                    // (LEFT / RIGHT / SMALL) so the game accepts the arena cab topology
                    GetDisplayConfigBufferSizes_orig =
                        detour::iat_try("GetDisplayConfigBufferSizes",
                            GetDisplayConfigBufferSizes_hook, avs::game::DLL_INSTANCE);
                    QueryDisplayConfig_orig =
                        detour::iat_try("QueryDisplayConfig",
                            QueryDisplayConfig_hook, avs::game::DLL_INSTANCE);
                    DisplayConfigGetDeviceInfo_orig =
                        detour::iat_try("DisplayConfigGetDeviceInfo",
                            DisplayConfigGetDeviceInfo_hook, avs::game::DLL_INSTANCE);
                }
#endif

            }
        }

        // window patch
        if (!is_arena_model()) {
            HMODULE gdme_module = libutils::try_module("libgdme.dll");
            if (GRAPHICS_WINDOWED && !replace_pattern(
                    gdme_module,
                    "754185ED753D8B4118BF0000CB02",
                    "9090????9090??????????????12", 0, 0)) {
                log_warning("gitadora", "windowed mode failed");
            }
        }

        // two channel mod
        if (TWOCHANNEL) {
            if (is_arena_model()) {
                log_warning("gitadora", "two channel audio (-2ch) is not supported on Arena Model - use a patch instead");
                deferredlogs::defer_error_messages({
                    "two channel audio (-2ch) is not supported on Arena Model - use a patch instead",
                    });

            } else {
                HMODULE bmsd_engine_module = libutils::try_module("libbmsd-engine.dll");
                HMODULE bmsd_module = libutils::try_module("libbmsd.dll");

                bmsd2_boot_orig = detour::iat_try("bmsd2_boot", bmsd2_boot_hook, bmsd_module);
                if (!(replace_pattern(bmsd_engine_module, "33000000488D", "03??????????", 0, 0) ||
                        replace_pattern(bmsd_engine_module, "330000000F10", "03??????????", 0, 0))) {
                    log_warning("gitadora", "two channel mode failed");
                }
            }
        }

#endif

    }
}
