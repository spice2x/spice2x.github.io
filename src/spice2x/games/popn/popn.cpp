#include "avs/game.h"
#include "popn.h"
#include "bi3a_hook.h"
#include <cstdint>
#include <cstring>
#include "hooks/graphics/graphics.h"
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/utils.h"
#include "cfg/button.h"
#include "cfg/api.h"
#include "hooks/setupapihook.h"
#include "hooks/sleephook.h"
#include "launcher/launcher.h"
#include "launcher/logger.h"
#include "misc/eamuse.h"
#include "util/sysutils.h"
#include "io.h"
#include "util/deferlog.h"
#include "misc/wintouchemu.h"

namespace games::popn {

    bool SHOW_PIKA_MONITOR_WARNING = false;
    
#if SPICE64 && !SPICE_XP

    const HMONITOR FAKE_MONITOR_HMONITOR = (HMONITOR)-1;
    constexpr LONG FAKE_MONITOR_OFFSET_X_Y = -999999;
    constexpr LONG FAKE_MONITOR_WIDTH = 1280;
    constexpr LONG FAKE_MONITOR_HEIGHT = 800;

    // pin 0 = buttons 1-3, size 9 (but it's actually triple that in reality - game passes in wrong length), values 0-0xFF
    // pin 1 = left pop (red pop kun), size 16, values 0-0xFF
    // pin 2 = right pop (blue pop kun), size 16, values 0-0xFF
    // pin 3 = title, size 44, values 0-0xFF
    // pin 4 = speaker, size 40, values 0-0xFF - U region only
    // pin 5 = monitor, size 18, values 0-0x7F - U region only
    // pin 6 = control panel, size 28, values 0-0x7F - U region only
    // pin 7 = cabinet, size 22, values 0-0x7F - U region only
    tapeledutils::tape_led TAPELED_MAPPING[POPN_TAPELED_TOTAL] = {
        {
            0, // these are button lights and handled specially
            0,
            0,
            0,
            "Invalid"
        },
        {
            5, // 5*3; game reports 16 but make it 15
            Lights::popn_lights_t::RedPopKun_R,
            Lights::popn_lights_t::RedPopKun_G,
            Lights::popn_lights_t::RedPopKun_B,
            "Red Pop-Kun"
        },
        {
            5, // 5*3; game reports 16 but make it 15
            Lights::popn_lights_t::BluePopKun_R,
            Lights::popn_lights_t::BluePopKun_G,
            Lights::popn_lights_t::BluePopKun_B,
            "Blue Pop-Kun"
        },
        {
            14, // 14*3; game reports 44 but make it 42; these are lights above the banner
            Lights::popn_lights_t::TopLED_R,
            Lights::popn_lights_t::TopLED_G,
            Lights::popn_lights_t::TopLED_B,
            "Top"
        },
    };


    static decltype(GetDisplayConfigBufferSizes) *GetDisplayConfigBufferSizes_orig = nullptr;
    static decltype(QueryDisplayConfig) *QueryDisplayConfig_orig = nullptr;
    static decltype(DisplayConfigGetDeviceInfo) *DisplayConfigGetDeviceInfo_orig = nullptr;

    static UINT32 pNumPathArrayElements_original = 0;
    static UINT32 pNumModeInfoArrayElements_original = 0;

    static
    LONG
    WINAPI
    GetDisplayConfigBufferSizes_hook(
        UINT32 Flags,
        UINT32 *pNumPathArrayElements,
        UINT32 *pNumModeInfoArrayElements)
    {
        const auto ret = GetDisplayConfigBufferSizes_orig(Flags, pNumPathArrayElements, pNumModeInfoArrayElements);
        if (FAKE_SUBSCREEN_ADAPTER) {
            log_misc("popn", "GetDisplayConfigBufferSizes returning fake monitor paths and modes");
            pNumPathArrayElements_original = *pNumPathArrayElements;
            pNumModeInfoArrayElements_original = *pNumModeInfoArrayElements;
            *pNumPathArrayElements += 1;
            *pNumModeInfoArrayElements += 2;
        }
        return ret;
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

        if (!FAKE_SUBSCREEN_ADAPTER) {
            return QueryDisplayConfig_orig(
                flags,
                numPathArrayElements, pathArray,
                numModeInfoArrayElements, modeInfoArray,
                currentTopologyId);
        }

        // call original to fill in real monitor info
        UINT32 num_paths = pNumPathArrayElements_original;
        UINT32 num_modes = pNumModeInfoArrayElements_original;
        const auto ret = QueryDisplayConfig_orig(
            flags,
            &num_paths, pathArray,
            &num_modes, modeInfoArray,
            currentTopologyId);

        if (ret != ERROR_SUCCESS) {
            log_warning("popn", "QueryDisplayConfig failed with error code {}", ret);
            return ret;
        }

        log_misc("popn", "QueryDisplayConfig returning fake monitor paths and modes");

        // insert a fake path
        DISPLAYCONFIG_PATH_INFO *path = &pathArray[pNumPathArrayElements_original];
        *path = {};
        path->flags = DISPLAYCONFIG_PATH_ACTIVE;

        path->sourceInfo.adapterId.HighPart = -1;
        path->sourceInfo.adapterId.LowPart = -1;
        path->sourceInfo.id = -1;
        path->sourceInfo.modeInfoIdx = pNumModeInfoArrayElements_original;
        path->sourceInfo.statusFlags = DISPLAYCONFIG_SOURCE_IN_USE;

        path->targetInfo.adapterId.HighPart = -1;
        path->targetInfo.adapterId.LowPart = -1;
        path->targetInfo.id = -2;
        path->targetInfo.modeInfoIdx = pNumModeInfoArrayElements_original + 1;
        path->targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
        path->targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        path->targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
        path->targetInfo.refreshRate.Numerator = 60000;
        path->targetInfo.refreshRate.Denominator = 1000;
        path->targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        path->targetInfo.targetAvailable = true;
        path->targetInfo.statusFlags = DISPLAYCONFIG_TARGET_IN_USE;

        // insert fake mode source
        DISPLAYCONFIG_MODE_INFO *mode_source = &modeInfoArray[pNumModeInfoArrayElements_original];
        *mode_source = {};
        mode_source->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        mode_source->id = -1;
        mode_source->adapterId.HighPart = -1;
        mode_source->adapterId.LowPart = -1;
        mode_source->sourceMode.width = FAKE_MONITOR_WIDTH;
        mode_source->sourceMode.height = FAKE_MONITOR_HEIGHT;
        mode_source->sourceMode.pixelFormat = DISPLAYCONFIG_PIXELFORMAT_32BPP;
        mode_source->sourceMode.position.x = FAKE_MONITOR_OFFSET_X_Y;
        mode_source->sourceMode.position.y = FAKE_MONITOR_OFFSET_X_Y;

        // insert fake mode target
        DISPLAYCONFIG_MODE_INFO *mode_target = &modeInfoArray[pNumModeInfoArrayElements_original+1];
        *mode_target = {};
        mode_target->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        mode_target->id = -2;
        mode_target->adapterId.HighPart = -1;
        mode_target->adapterId.LowPart = -1;
        return ret;
    }

    static
    LONG
    WINAPI
    DisplayConfigGetDeviceInfo_hook(DISPLAYCONFIG_DEVICE_INFO_HEADER* requestPacket)
    {
        if (requestPacket == nullptr) {
            return DisplayConfigGetDeviceInfo_orig(requestPacket);
        }

        // fake monitor
        if (FAKE_SUBSCREEN_ADAPTER &&
            (requestPacket->id == static_cast<UINT32>(-1) ||
             requestPacket->id == static_cast<UINT32>(-2)) &&
            requestPacket->adapterId.HighPart == static_cast<LONG>(-1) &&
            requestPacket->adapterId.LowPart == static_cast<DWORD>(-1)) {
            log_misc(
                "popn",
                "DisplayConfigGetDeviceInfo hook hit for fake monitor, type={}, size={}, id={}, luid={}/{}",
                static_cast<int>(requestPacket->type),
                requestPacket->size,
                requestPacket->id,
                requestPacket->adapterId.HighPart,
                requestPacket->adapterId.LowPart);

            if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
                const auto target = reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(requestPacket);
                target->flags.value = 0;
                target->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
                target->connectorInstance = 0;
                wcscpy(target->monitorFriendlyDeviceName, L"Spice Fake Monitor");
                wcscpy(target->monitorDevicePath, L"\\\\?\\SpiceFakeMonitor");
            } else if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME) {
                const auto source = reinterpret_cast<DISPLAYCONFIG_SOURCE_DEVICE_NAME*>(requestPacket);
                // value must match WrappedIDirect3D9::GetAdapterIdentifier
                wcscpy(source->viewGdiDeviceName, L"\\\\.\\DISPLAY_SPICE_FAKE");
            } else {
                log_fatal(
                    "popn",
                    "unexpected device info type {} for fake monitor",
                    static_cast<int>(requestPacket->type));
            }

            return ERROR_SUCCESS;
        }

        const auto ret = DisplayConfigGetDeviceInfo_orig(requestPacket);
        log_misc(
            "popn",
            "DisplayConfigGetDeviceInfo returned {}, type={}, size={}, id={}, luid={}/{}",
            ret,
            static_cast<int>(requestPacket->type),
            requestPacket->size,
            requestPacket->id,
            requestPacket->adapterId.HighPart,
            requestPacket->adapterId.LowPart);
        if (ret == ERROR_SUCCESS) {
            if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME) {
                const auto sourceName = reinterpret_cast<DISPLAYCONFIG_SOURCE_DEVICE_NAME*>(requestPacket);
                log_misc(
                    "popn",
                    "... name={}",
                    ws2s(sourceName->viewGdiDeviceName));
            } else if (requestPacket->type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME) {
                const auto targetName = reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(requestPacket);
                log_misc(
                    "popn",
                    "... flags={}, outputTechnology: {}, connectorInstance={}, friendlyname={} path={}",
                    targetName->flags.value,
                    static_cast<int>(targetName->outputTechnology),
                    targetName->connectorInstance,
                    ws2s(targetName->monitorFriendlyDeviceName),
                    ws2s(targetName->monitorDevicePath));

                // need to fix up some values for the primary monitor...
                const auto monitors = sysutils::enumerate_monitors();
                for (const auto& monitor : monitors) {
                    if (monitor.id == targetName->header.id &&
                        monitor.adapter_id_HighPart == targetName->header.adapterId.HighPart &&
                        monitor.adapter_id_LowPart == targetName->header.adapterId.LowPart) {

                        if (monitor.is_primary) {
                            targetName->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
                            targetName->connectorInstance = 2;
                            log_info(
                                "popn",
                                "... overriding primary monitor ({}) to pretend to be DP port #2",
                                monitor.display_name);
                        } else {
                            // TODO: is this what the game expects for subscreen?
                            // TODO: what if there are 3+ monitors?
                            targetName->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
                            targetName->connectorInstance = 0;
                        }
                        break;
                    }
                }
            }
        }
        return ret;
    }

#endif

#if !SPICE64

    static int __cdecl usbCheckAlive() {
        return 1;
    }

    static int __cdecl usbCheckSecurityNew() {
        return 0;
    }

    static int __cdecl usbCoinGet() {
        return eamuse_coin_get_stock();
    }

    static int __cdecl usbCoinMode() {
        return 0;
    }

    static int __cdecl usbEnd() {
        return 0;
    }

    static int __cdecl usbFirmResult() {
        return 0;
    }

    static int __cdecl usbGetKEYID() {
        return 0;
    }

    static int __cdecl usbGetSecurity() {
        return 0;
    }

    static int __cdecl usbLamp(uint32_t lamp_bits) {

        // get lights
        auto &lights = get_lights();

        // bit scan
        static const size_t mapping[] = {
                Lights::Button1,
                Lights::Button2,
                Lights::Button3,
                Lights::Button4,
                Lights::Button5,
                Lights::Button6,
                Lights::Button7,
                Lights::Button8,
                Lights::Button9,
                Lights::HiLamp1,
                Lights::HiLamp2,
                Lights::HiLamp3,
                Lights::HiLamp4,
                Lights::HiLamp5,
                Lights::LeftLamp1,
                Lights::LeftLamp2,
                Lights::RightLamp1,
                Lights::RightLamp2,
        };

        // sw lamps
        for (size_t i = 0; i < 9; i++) {
            float value = (lamp_bits & (1 << (i + 23))) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
        }
        // neon lamps
        for (size_t i = 0; i < 5; i++) {
            float value = (lamp_bits & (1 << i)) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i + 9]), value);
        }
        // screen lamps
        for (size_t i = 0; i < 4; i++) {
            float value = (lamp_bits & (1 << (i + 8))) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i + 14]), value);
        }

        // flush output
        RI_MGR->devices_flush_output();

        // return no error
        return 0;
    }

    static int __cdecl usbPadRead(unsigned int *pad_bits) {

        // get buttons
        auto &buttons = get_buttons();

        // reset
        *pad_bits = 0;

        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Service))) {
            *pad_bits |= 1 << 6;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Test))) {
            *pad_bits |= 1 << 7;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::CoinMech))) {
            *pad_bits |= 1 << 22;
        }

        // buttons
        static const size_t mapping[] = {
                Buttons::Button1,
                Buttons::Button2,
                Buttons::Button3,
                Buttons::Button4,
                Buttons::Button5,
                Buttons::Button6,
                Buttons::Button7,
                Buttons::Button8,
                Buttons::Button9,
        };
        for (size_t i = 0; i < 9; i++) {
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(mapping[i]))) {
                *pad_bits |= 1 << (8 + i);
            }
        }

        // return no error
        return 0;
    }

    static int __cdecl usbPadReadLast(uint8_t *a1) {
        memset(a1, 0, 40);
        return 0;
    }

    static int __cdecl usbSecurityInit() {
        return 0;
    }

    static int __cdecl usbSecurityInitDone() {
        return 0;
    }

    static int __cdecl usbSecuritySearch() {
        return 0;
    }

    static int __cdecl usbSecuritySearchDone() {
        return 0;
    }

    static int __cdecl usbSecuritySelect() {
        return 0;
    }

    static int __cdecl usbSecuritySelectDone() {
        return 0;
    }

    static int __cdecl usbSetExtIo(uint32_t rgb_bits) {

        // get lights
        auto &lights = get_lights();

        // bit scan
        static const size_t mapping[] = {
                Lights::WooferLED_R,
                Lights::WooferLED_G,
                Lights::WooferLED_B,
        };
        
        for (size_t i = 0; i < 3; i++) {
            float value = (rgb_bits & (1 << (i + 1)));
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
        }
    
        // flush output
        RI_MGR->devices_flush_output();

        return 0;
    }

    static int __cdecl usbStart() {
        return 0;
    }

    static int __cdecl usbWdtReset() {
        return 0;
    }

    static int __cdecl usbWdtStart() {
        return 0;
    }

    static int __cdecl usbWdtStartDone() {
        return 0;
    }

    static decltype(DeleteFileA)* DeleteFileA_real = nullptr;

    static BOOL WINAPI DeleteFileA_hook(LPCSTR lpFileName) {

        // fantasia deletes a bunch of drmfs files in dll_entry_init
        if (string_begins_with(lpFileName, "D:\\L39\\contents")) {
            return true;
        }

        return DeleteFileA_real(lpFileName);
    }

    static decltype(mixerSetControlDetails)* mixerSetControlDetails_real = nullptr;

    static MMRESULT WINAPI mixerSetControlDetails_hook(HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails) {

        mixerSetControlDetails_real(hmxobj, pmxcd, fdwDetails);
        return MMSYSERR_NOERROR;
    }

#endif

    static bool log_hook(void *user, const std::string &data, logger::Style style, std::string &out) {

        // get rid of the log spam
        if (data.find(" I:CabinetSettingFile: label:") != std::string::npos) {
            out.clear();
            return true;
        } else {
            return false;
        }
    }

    POPNGame::POPNGame() : Game("Pop'n Music") {
        logger::hook_add(log_hook, this);
    }

    POPNGame::~POPNGame() {
        logger::hook_remove(log_hook, this);
    }

    void POPNGame::pre_attach() {
        Game::pre_attach();

#if !SPICE64

        // only needed for older versions (19-21, inclusive) but create them anyway since 21 and above are all M39
        fileutils::dir_create_recursive_log("popn", "dev\\raw\\bookkeeping");
        fileutils::dir_create_recursive_log("popn", "dev\\raw\\ranking");
        fileutils::dir_create_recursive_log("popn", "dev\\raw\\settings");

        // load without resolving references
        // makes game not trigger DLLMain which results in an error due to missing EZUSB device
        LoadLibraryExW((MODULE_PATH / "ezusb.dll").c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);

#endif

    }

    void POPNGame::attach() {
        Game::attach();

#if !SPICE64
        /*
         * Fast Boot (TM) Patch
         * Game tries to create some directories and if it fails it will sleep for 1 second
         * We make it sleep 1ms each time instead
         */
        hooks::sleep::init(999, 1);

        DeleteFileA_real = detour::iat_try("DeleteFileA", DeleteFileA_hook, avs::game::DLL_INSTANCE);
        mixerSetControlDetails_real = detour::iat_try("mixerSetControlDetails", mixerSetControlDetails_hook, avs::game::DLL_INSTANCE);

        // EZUSB hooks
        HINSTANCE ezusb = libutils::try_library("ezusb.dll");
        if (ezusb) {
            detour::inline_hook((void *) usbCheckAlive,
                                libutils::try_proc(ezusb, "?usbCheckAlive@@YAHXZ"));
            detour::inline_hook((void *) usbCheckSecurityNew,
                                libutils::try_proc(ezusb, "?usbCheckSecurityNew@@YAHH@Z"));
            detour::inline_hook((void *) usbCoinGet,
                                libutils::try_proc(ezusb, "?usbCoinGet@@YAHH@Z"));
            detour::inline_hook((void *) usbCoinMode,
                                libutils::try_proc(ezusb, "?usbCoinMode@@YAHH@Z"));
            detour::inline_hook((void *) usbEnd,
                                libutils::try_proc(ezusb, "?usbEnd@@YAHXZ"));
            detour::inline_hook((void *) usbFirmResult,
                                libutils::try_proc(ezusb, "?usbFirmResult@@YAHXZ"));
            detour::inline_hook((void *) usbGetKEYID,
                                libutils::try_proc(ezusb, "?usbGetKEYID@@YAHPAEH@Z"));
            detour::inline_hook((void *) usbGetSecurity,
                                libutils::try_proc(ezusb, "?usbGetSecurity@@YAHHPAE@Z"));
            detour::inline_hook((void *) usbLamp,
                                libutils::try_proc(ezusb, "?usbLamp@@YAHH@Z"));
            detour::inline_hook((void *) usbPadRead,
                                libutils::try_proc(ezusb, "?usbPadRead@@YAHPAK@Z"));
            detour::inline_hook((void *) usbPadReadLast,
                                libutils::try_proc(ezusb, "?usbPadReadLast@@YAHPAE@Z"));
            detour::inline_hook((void *) usbSecurityInit,
                                libutils::try_proc(ezusb, "?usbSecurityInit@@YAHXZ"));
            detour::inline_hook((void *) usbSecurityInitDone,
                                libutils::try_proc(ezusb, "?usbSecurityInitDone@@YAHXZ"));
            detour::inline_hook((void *) usbSecuritySearch,
                                libutils::try_proc(ezusb, "?usbSecuritySearch@@YAHXZ"));
            detour::inline_hook((void *) usbSecuritySearchDone,
                                libutils::try_proc(ezusb, "?usbSecuritySearchDone@@YAHXZ"));
            detour::inline_hook((void *) usbSecuritySelect,
                                libutils::try_proc(ezusb, "?usbSecuritySelect@@YAHH@Z"));
            detour::inline_hook((void *) usbSecuritySelectDone,
                                libutils::try_proc(ezusb, "?usbSecuritySelectDone@@YAHXZ"));
            detour::inline_hook((void *) usbSetExtIo,
                                libutils::try_proc(ezusb, "?usbSetExtIo@@YAHH@Z"));
            detour::inline_hook((void *) usbStart,
                                libutils::try_proc(ezusb, "?usbStart@@YAHH@Z"));
            detour::inline_hook((void *) usbWdtReset,
                                libutils::try_proc(ezusb, "?usbWdtReset@@YAHXZ"));
            detour::inline_hook((void *) usbWdtStart,
                                libutils::try_proc(ezusb, "?usbWdtStart@@YAHH@Z"));
            detour::inline_hook((void *) usbWdtStartDone,
                                libutils::try_proc(ezusb, "?usbWdtStartDone@@YAHXZ"));
        }

#endif

#if SPICE64 && !SPICE_XP
        if (!GRAPHICS_WINDOWED && D3D9_ADAPTER.has_value()) {
            SHOW_PIKA_MONITOR_WARNING = true;
            log_warning(
                "popn",
                "\n\n"
                "!!! using -dxmainadapter option is NOT recommended due to known     !!!\n"
                "!!! compatibility issues with the game                              !!!\n"
                "!!!                                                                 !!!\n"
                "!!!   * game may launch in wrong resolution or refresh rate         !!!\n"
                "!!!   * touch / mouse input may stop working in subscreen / overlay !!!\n"
                "!!!                                                                 !!!\n"
                "!!! recommendation is to use the Change Main Monitor (-mainmonitor) !!!\n"
                "!!! option instead of -dxmainadapter                                !!!\n\n"
                );

            deferredlogs::defer_error_messages({
                "-dxmainadapter option is NOT recommended due to known compatibility ",
                " issues with the game!",
                "      * game may launch in wrong resolution or refresh rate",
                "      * touch / mouse input may stop working in subscreen / overlay",
                "    use Change Main Monitor (-mainmonitor) option instead"
                });
        }

        // monitor hook
        GetDisplayConfigBufferSizes_orig =
            detour::iat_try("GetDisplayConfigBufferSizes",
                GetDisplayConfigBufferSizes_hook, avs::game::DLL_INSTANCE);
        QueryDisplayConfig_orig =
            detour::iat_try("QueryDisplayConfig",
                QueryDisplayConfig_hook, avs::game::DLL_INSTANCE);
        DisplayConfigGetDeviceInfo_orig =
            detour::iat_try("DisplayConfigGetDeviceInfo",
                DisplayConfigGetDeviceInfo_hook, avs::game::DLL_INSTANCE);

        // GUID_DEVCLASS_USB = {86E0D1E0-11D0-89B0-00A0C9054129}
        SETUPAPI_SETTINGS settings{};
        settings.class_guid[0] = 0x86E0D1E0;
        settings.class_guid[1] = 0x11D08089;
        settings.class_guid[2] = 0x0008E49C;
        settings.class_guid[3] = 0x731F303E;

        // BIO3 - 8058 (maybe U region cabs with more lights?)
        // BIO3 - 8054 (probably J region?)
        // BIO2 - 8050 (need bi2x_hook, everything will work except buttons 1-9)

        const char property[] = "1CCF(8058)_000";
        const char property_hardwareid[] = "USB\\VID_1CCF&PID_8058&MI_00\\000";
        memcpy(settings.property_devicedesc, property, sizeof(property));
        memcpy(settings.property_hardwareid, property_hardwareid, sizeof(property_hardwareid));
        setupapihook_init(avs::game::DLL_INSTANCE);
        setupapihook_add(settings);

        bi3a_hook_init();
        
        // note: if for whatever reason you want to use BIO2, the game needs to be patched
        //       check popn.dll and look for static table that looks like this:
        //       00000001 00000003 00000001  (button 1)
        //       00000002 00000004 00000001  (button 2)
        //       00000004 00000005 00000001  (button 3)
        //       00000008 00000006 00000001  (button 4)
        //       00000010 00000007 00000001  (button 5)
        //       00000020 00000008 00000001  (button 6)
        //       00000040 00000009 00000001  (button 7)
        //       00000080 0000000A 00000001  (button 8)
        //       00000100 0000000B 00000001  (button 9)
        //       set third column to 0 and it will work with BIO2
        
        if (!GRAPHICS_WINDOWED) {
            wintouchemu::FORCE = true;
            wintouchemu::INJECT_MOUSE_AS_WM_TOUCH = true;
            wintouchemu::hook_title_ends("", "Main Screen", avs::game::DLL_INSTANCE);
        }

        sysutils::hook_EnumDisplayDevicesA();

#endif

    }
}
