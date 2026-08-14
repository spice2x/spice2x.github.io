#include "gitadora.h"
#include "asio.h"
#include "handle.h"
#include "bi2x_hook.h"
#include <span>
#include <unordered_map>

#include "cfg/configurator.h"
#include "cfg/screen_resize.h"

#include <ks.h>
#include <ksmedia.h>
#include "hooks/audio/audio.h"
#include "hooks/audio/mme.h"
#include "hooks/graphics/graphics.h"
#include "misc/wintouchemu.h"
#include "overlay/overlay.h"
#include "touch/native/nativetouchhook.h"
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
    bool DISABLE_FRAME_LIMITER = false;
    std::optional<unsigned int> CAB_TYPE = std::nullopt;
    bool P1_LEFTY = false;
    bool P2_LEFTY = false;
    std::optional<std::string> SUBSCREEN_OVERLAY_SIZE;
    std::optional<socd::SocdAlgorithm> PICK_ALGO = socd::SocdAlgorithm::PreferRecent;
    std::optional<uint8_t> ARENA_WINDOW_COUNT = std::nullopt;
    bool ARENA_TWO_HEAD_EXCLUSIVE = false;
    std::optional<std::string> ASIO_DRIVER = std::nullopt;
    bool ALLOW_REALTEK_AUDIO = false;
    bool NATIVE_TOUCH = false;

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

    // libshare-pj paces mainloop with separate 12 ms and 16 ms waits. these waits
    // interfere with the game's normal display synchronization on modern Windows
    // and can hold a nominal 60 FPS game near 58 FPS. locate the instruction
    // sequences at runtime so the fix does not depend on per-version file offsets.
    static void disable_mainloop_frame_limiter(HMODULE sharepj_module) {
        if (!sharepj_module) {
            return;
        }

        // both limiters have the same shape, only the millisecond target xx differs
        // (0Ch for the 12 ms limiter, 10h for the 16 ms one):
        //
        // 48 83 F8 xx:     cmp rax, xx;   compare elapsed frame time with the target
        // 73 10:           jae +10h;      skip the wait once the target has elapsed
        // B9 xx 00 00 00:  mov ecx, xx;   load the target
        // 48 2B C8:        sub rcx, rax;  calculate the remaining wait time
        // 74 06:           je +6h;        skip the following six-byte Sleep call if no wait remains
        //
        // changing jae (73h) to jmp (EBh) makes each block always take its existing
        // skip path, which bypasses only the associated Sleep call and leaves the 10h
        // branch displacement and every other wait untouched.
        const auto limiter_12ms_disabled = replace_pattern(
                sharepj_module,
                "4883F80C7310B90C000000482BC87406",
                "????????EB??????????????????????", 0, 0);
        const auto limiter_16ms_disabled = replace_pattern(
                sharepj_module,
                "4883F8107310B910000000482BC87406",
                "????????EB??????????????????????", 0, 0);

        if (!limiter_12ms_disabled || !limiter_16ms_disabled) {
            log_fatal(
                "gitadora",
                "failed to disable libshare-pj mainloop frame limiter (-gdnoframelimiter), ensure patch is not already applied");
            return;
        }

        log_info("gitadora", "successfully disabled libshare-pj mainloop frame limiter (-gdnoframelimiter)");
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

                // figure out default settings if user didn't provide one
                if (!ARENA_WINDOW_COUNT.has_value()) {
                    if (!GRAPHICS_WINDOWED && sysutils::enumerate_monitors().size() < 4) {
                        log_info("gitadora", "arena model: <4 monitors, defaulting to single window mode");
                        ARENA_WINDOW_COUNT = 1;
                    } else {
                        ARENA_WINDOW_COUNT = 4;
                    }
                }

                const int count = ARENA_WINDOW_COUNT.value();
                switch (count) {
                    case 1:
                        log_info("gitadora", "arena model: single-window mode");
                        GRAPHICS_FORCE_SINGLE_ADAPTER = true;
                        GRAPHICS_PREVENT_SECONDARY_WINDOWS = true;
                        break;
                    case 2:
                        if (!GRAPHICS_WINDOWED) {
                            if (D3D9_ADAPTER.has_value()) {
                                log_fatal(
                                    "gitadora",
                                    "arena model: fullscreen two-window mode cannot use -monitor; "
                                    "use -w for borderless windows instead");
                            }
                            ARENA_TWO_HEAD_EXCLUSIVE = true;
                            log_info(
                                "gitadora",
                                "arena model: native two-head fullscreen adapter-group mode "
                                "(MAIN + SMALL; LEFT/RIGHT are virtual)");
                        } else {
                            log_info(
                                "gitadora",
                                "arena model: two-window mode uses windowed rendering");
                        }
                        log_info("gitadora", "arena model: two-window mode");
                        GRAPHICS_GITADORA_HIDE_SIDE_WINDOWS = true;
                        break;
                    case 4:
                        log_info("gitadora", "arena model: four-window mode");
                        break;
                    default:
                        log_fatal(
                            "gitadora",
                            "arena model: unsupported window count: {}", count);
                }
            }

#endif

        }
    }

#if SPICE64 && !SPICE_XP

    static decltype(GetDisplayConfigBufferSizes) *GetDisplayConfigBufferSizes_orig = nullptr;
    static decltype(QueryDisplayConfig) *QueryDisplayConfig_orig = nullptr;
    static decltype(DisplayConfigGetDeviceInfo) *DisplayConfigGetDeviceInfo_orig = nullptr;

    // Cached real monitor paths and their source/target mode entries. modeInfoIdx values are
    // renumbered so the cache is self-contained. The normal single-head emulation keeps only
    // MAIN; the two-head adapter-group mode keeps MAIN and SMALL as real heads.
    static DISPLAYCONFIG_PATH_INFO real_primary_path = {};
    static DISPLAYCONFIG_MODE_INFO real_primary_modes[2] = {}; // [0]=source, [1]=target
    static DISPLAYCONFIG_PATH_INFO real_small_path = {};
    static DISPLAYCONFIG_MODE_INFO real_small_modes[2] = {}; // [0]=source, [1]=target
    static bool real_small_path_available = false;

    static bool is_two_head_exclusive() {
        return ARENA_TWO_HEAD_EXCLUSIVE && !GRAPHICS_WINDOWED;
    }

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
    // Normal single-head emulation: every non-MAIN role is fake.
    static constexpr FakeMonitor FAKE_MONITORS_SINGLE_HEAD[] = {
        { 1, 1080, 1920, -100000, -100000, 0 }, // left  (DP connector instance 0)
        { 2, 1080, 1920, -200000, -200000, 1 }, // right (DP connector instance 1)
        { 3,  800, 1280, -300000, -300000, 2 }, // small (DP connector instance 2, touch)
    };

    // Two-head fullscreen: adapters 0/1 are the real MAIN/SMALL group heads.
    // Keep only LEFT/RIGHT fake, with their adapter ids matching the D3D9 wrapper.
    static constexpr FakeMonitor FAKE_MONITORS_TWO_HEAD[] = {
        { 2, 1080, 1920, -200000, -200000, 0 }, // left  (DP connector instance 0)
        { 3, 1080, 1920, -300000, -300000, 1 }, // right (DP connector instance 1)
    };

    static std::span<const FakeMonitor> get_fake_monitors() {
        if (is_two_head_exclusive()) {
            return FAKE_MONITORS_TWO_HEAD;
        }

        return FAKE_MONITORS_SINGLE_HEAD;
    }

    static UINT32 real_monitor_count() {
        return is_two_head_exclusive() ? 2 : 1;
    }

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

        if (primary->targetInfo.modeInfoIdx >= all_modes.size()) {
            log_fatal("gitadora", "cache_primary_monitor_info: primary target mode is missing");
        }

        real_primary_modes[0] = all_modes[primary->sourceInfo.modeInfoIdx];
        real_primary_modes[1] = all_modes[primary->targetInfo.modeInfoIdx];
        real_primary_path = *primary;
        real_primary_path.sourceInfo.modeInfoIdx = 0;
        real_primary_path.targetInfo.modeInfoIdx = 1;

        real_small_path_available = false;
        if (is_two_head_exclusive()) {
            if (all_paths.size() != 2) {
                log_fatal(
                    "gitadora",
                    "two-head fullscreen mode requires exactly two active display paths, found {}",
                    all_paths.size());
            }

            const auto secondary = std::find_if(all_paths.begin(), all_paths.end(),
                [&](const auto &p) {
                    return &p != &*primary;
                });
            if (secondary == all_paths.end() ||
                    secondary->sourceInfo.modeInfoIdx >= all_modes.size() ||
                    secondary->targetInfo.modeInfoIdx >= all_modes.size()) {
                log_fatal("gitadora", "could not identify the physical SMALL monitor");
            }
            const LUID primary_adapter = primary->sourceInfo.adapterId;
            const LUID secondary_adapter = secondary->sourceInfo.adapterId;
            const bool same_adapter = primary_adapter.HighPart == secondary_adapter.HighPart &&
                primary_adapter.LowPart == secondary_adapter.LowPart;
            if (!same_adapter) {
                log_fatal(
                    "gitadora",
                    "MAIN and SMALL must use the same display adapter");
            }

            real_small_modes[0] = all_modes[secondary->sourceInfo.modeInfoIdx];
            real_small_modes[1] = all_modes[secondary->targetInfo.modeInfoIdx];
            real_small_path = *secondary;
            real_small_path.sourceInfo.modeInfoIdx = 2;
            real_small_path.targetInfo.modeInfoIdx = 3;
            real_small_path_available = true;

            log_info("gitadora", "cache_primary_monitor_info: cached real MAIN and SMALL monitors");
        } else {
            log_info("gitadora", "cache_primary_monitor_info: cached primary monitor");
        }
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

        const auto fake_monitors = get_fake_monitors();
        const auto fake_count = static_cast<UINT32>(fake_monitors.size());
        const UINT32 real_count = real_monitor_count();
        *pNumPathArrayElements = real_count + fake_count;
        *pNumModeInfoArrayElements = (real_count + fake_count) * 2;

        log_info(
            "gitadora",
            "GetDisplayConfigBufferSizes: {} real path(s) + {} fake monitor(s)",
            real_count,
            fake_count);

        return ERROR_SUCCESS;
    }

    // Write a fake monitor after the real paths. The cache is packed as [source, target]
    // mode pairs, so each path consumes two mode entries.
    static void insert_fake_monitor(
        DISPLAYCONFIG_PATH_INFO *paths,
        DISPLAYCONFIG_MODE_INFO *modes,
        const FakeMonitor &m,
        UINT32 path_index,
        UINT32 source_mode_index)
    {
        const UINT32 src_idx = source_mode_index;
        const UINT32 tgt_idx = src_idx + 1;
        const LUID adapter_id { .LowPart = static_cast<DWORD>(-m.id), .HighPart = -m.id };
        const UINT32 uid = static_cast<UINT32>(-m.id);

        paths[path_index] = {
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
        // Copy cached real monitor paths into the caller buffers.
        pathArray[0] = real_primary_path;
        modeInfoArray[0] = real_primary_modes[0];
        modeInfoArray[1] = real_primary_modes[1];
        if (is_two_head_exclusive() && real_small_path_available) {
            pathArray[1] = real_small_path;
            modeInfoArray[2] = real_small_modes[0];
            modeInfoArray[3] = real_small_modes[1];
        }

        const auto fake_monitors = get_fake_monitors();
        const auto fake_count = static_cast<UINT32>(fake_monitors.size());
        const UINT32 real_count = real_monitor_count();
        *numPathArrayElements = real_count + fake_count;
        *numModeInfoArrayElements = (real_count + fake_count) * 2;
        if (currentTopologyId != nullptr) {
            *currentTopologyId = DISPLAYCONFIG_TOPOLOGY_EXTEND;
        }

        log_misc("gitadora", "QueryDisplayConfig returning fake monitor paths and modes");

        // Append fake monitors after the real path(s).
        for (UINT32 i = 0; i < fake_count; i++) {
            insert_fake_monitor(
                pathArray,
                modeInfoArray,
                fake_monitors[i],
                real_count + i,
                real_count * 2 + i * 2);
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
                const auto fake_monitors = get_fake_monitors();
                for (const auto &f : fake_monitors) {
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

        // Override MAIN to HDMI/0 and retag the real second head as the DP/2 SMALL
        // display expected by the cabinet software in two-head fullscreen mode.
        const auto targetName = reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(requestPacket);
        const auto target_matches = [&](const DISPLAYCONFIG_PATH_TARGET_INFO &target) {
            return target.id == targetName->header.id &&
                target.adapterId.HighPart == targetName->header.adapterId.HighPart &&
                target.adapterId.LowPart == targetName->header.adapterId.LowPart;
        };
        if (target_matches(real_primary_path.targetInfo)) {
            targetName->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
            targetName->connectorInstance = 0;
            log_info("gitadora",
                "overriding primary monitor (id={}) to pretend to be HDMI",
                targetName->header.id);
        } else if (is_two_head_exclusive() && real_small_path_available &&
                target_matches(real_small_path.targetInfo)) {
            targetName->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
            targetName->connectorInstance = 2;
            log_info("gitadora",
                "overriding secondary monitor (id={}) to pretend to be SMALL DP/2",
                targetName->header.id);
        }
        return ret;
    }

#endif

    void GitaDoraGame::attach() {
        Game::attach();

        // arena model launches a tiny window yet backbuffer at 4k, resulting in unusable overlay
        // force scaling to make things usable
        if (!overlay::UI_SCALE_PERCENT.has_value() && is_arena_model()) {
            log_info("gitadora", "forcing UI scale to 250% for arena model");
            overlay::UI_SCALE_PERCENT = 250;
        }

        // modules
        HMODULE sharepj_module = libutils::try_module("libshare-pj.dll");
        HMODULE bmsd2_module = libutils::try_module("libbmsd2.dll");
        HMODULE system_module = libutils::try_module("libsystem.dll");

        // patches
#ifdef SPICE64
        if (DISABLE_FRAME_LIMITER && !is_arena_model()) {
            disable_mainloop_frame_limiter(sharepj_module);
        }
#endif

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

        // before we start patching and hooking things, detect invalid configuration
        if (aio != nullptr && !is_arena_model()) {
            log_fatal("gitadora", "arena model i/o (libaio.dll) detected, but <spec> is not an arena model - bad prop XML files?");
        }

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

            // ASIO driver redirect (XONAR -> user-configured driver)
            asio_hook_init();

            // volume change prevention
            hooks::audio::mme::init(avs::game::DLL_INSTANCE);

            // fake Realtek audio injection
            // if ASIO init succeeds, game tries to look for audio device with `Realtek` in friendly name
            // if ASIO init fails, game opens default audio device
            // therefore, it's safe to enable this hook by default regardless of ASIO preference
            // (unless the user explicitly disables it, of course)
            if (ALLOW_REALTEK_AUDIO) {
                log_info(
                    "gitadora",
                    "fake Realtek audio injection disabled "
                    "(user's real Realtek audio may be used after successful ASIO init)");
            } else {
                log_info(
                    "gitadora",
                    "fake Realtek audio injection enabled "
                    "(create a fake Realtek audio device to prevent crashes after successful ASIO init)");
                hooks::audio::INJECT_FAKE_REALTEK_AUDIO = true;
            }

            // touch injection drives mouse-as-touch and API touch for the subscreen,
            // no matter whether it is drawn by the overlay (single-window mode) or by
            // the dedicated SMALL window
            NATIVE_TOUCH = !wintouchemu::FORCE &&
                nativetouch::hook(avs::game::DLL_INSTANCE);
            if (!NATIVE_TOUCH && GRAPHICS_PREVENT_SECONDARY_WINDOWS) {
                // the legacy fallback can only feed the subscreen overlay
                wintouchemu::FORCE = true;
                wintouchemu::INJECT_MOUSE_AS_WM_TOUCH = true;
                wintouchemu::hook("GITADORA", avs::game::DLL_INSTANCE);
            }

#if !SPICE_XP

            if (!GRAPHICS_WINDOWED &&
                    (GRAPHICS_PREVENT_SECONDARY_WINDOWS || ARENA_TWO_HEAD_EXCLUSIVE)) {
                if (ARENA_TWO_HEAD_EXCLUSIVE) {
                    log_info(
                        "gitadora",
                        "exposing physical MAIN/SMALL and virtual LEFT/RIGHT");
                }
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
        if (TWOCHANNEL && !is_arena_model()) {
            HMODULE bmsd_engine_module = libutils::try_module("libbmsd-engine.dll");
            HMODULE bmsd_module = libutils::try_module("libbmsd.dll");

            bmsd2_boot_orig = detour::iat_try("bmsd2_boot", bmsd2_boot_hook, bmsd_module);
            if (!(replace_pattern(bmsd_engine_module, "33000000488D", "03??????????", 0, 0) ||
                    replace_pattern(bmsd_engine_module, "330000000F10", "03??????????", 0, 0))) {
                log_warning("gitadora", "two channel mode failed");
            }
        }

#endif

    }

    void fix_audio_channel_mask(WAVEFORMATEX *format) {
        if (!format || format->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
            return;
        }

        auto ext = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);

        // fix the legacy 7.1 channel mask to the modern surround layout
        // makes it more compatible with modern audio cards
        if (ext->dwChannelMask == KSAUDIO_SPEAKER_7POINT1) {
            ext->dwChannelMask = KSAUDIO_SPEAKER_7POINT1_SURROUND;
        }
    }
}
