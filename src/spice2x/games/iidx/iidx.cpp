#include "iidx.h"

#include "acioemu/handle.h"
#include "avs/core.h"
#include "avs/game.h"
#include "cfg/configurator.h"
#include "games/io.h"

#include "games/iidx/legacy_camera.h"

#include "hooks/audio/audio.h"
#include "hooks/avshook.h"
#include "hooks/cfgmgr32hook.h"
#include "hooks/devicehook.h"
#include "hooks/graphics/graphics.h"
#ifdef SPICE64
#include "hooks/graphics/nvenc_hook.h"
#endif
#include "hooks/setupapihook.h"
#include "hooks/sleephook.h"
#include "launcher/options.h"
#include "touch/touch.h"
#include "misc/wintouchemu.h"
#include "misc/eamuse.h"
#include "util/detour.h"
#include "util/deferlog.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/memutils.h"
#include "util/sigscan.h"
#include "util/utils.h"
#include "launcher/signal.h"

#include "external/robin_hood.h"

#include "bi2a.h"
#include "bi2x_hook.h"
#include "ezusb.h"
#include "io.h"

static decltype(RegCloseKey) *RegCloseKey_orig = nullptr;
static decltype(RegEnumKeyA) *RegEnumKeyA_orig = nullptr;
static decltype(RegOpenKeyA) *RegOpenKeyA_orig = nullptr;
static decltype(RegOpenKeyExA) *RegOpenKeyExA_orig = nullptr;
static decltype(RegQueryValueExA) *RegQueryValueExA_orig = nullptr;

namespace games::iidx {

    // constants
    const HKEY PARENT_ASIO_REG_HANDLE = reinterpret_cast<HKEY>(0x3001);
    const HKEY DEVICE_ASIO_REG_HANDLE = reinterpret_cast<HKEY>(0x3002);
    const char *ORIGINAL_ASIO_DEVICE_NAME = "XONAR SOUND CARD(64)";

    // settings
    bool FLIP_CAMS = false;
    std::optional<bool> DISABLE_CAMS;
    bool TDJ_CAMERA = false;
    bool TDJ_CAMERA_PREFER_16_9 = true;
    bool TDJ_MODE = false;
    bool FORCE_720P = false;
    bool DISABLE_ESPEC_IO = false;
    bool NATIVE_TOUCH = false;
    std::optional<std::string> SOUND_OUTPUT_DEVICE = std::nullopt;
    std::optional<std::string> SOUND_OUTPUT_DEVICE_IN_EFFECT = std::nullopt;
    std::optional<std::string> ASIO_DRIVER = std::nullopt;
    uint8_t DIGITAL_TT_SENS = 4;
    std::optional<std::string> SUBSCREEN_OVERLAY_SIZE = std::nullopt;
    std::optional<std::string> SCREEN_MODE = std::nullopt;
    std::optional<std::string> TDJ_CAMERA_OVERRIDE = std::nullopt;

    // states
    static HKEY real_asio_reg_handle = nullptr;
    static HKEY real_asio_device_reg_handle = nullptr;
    static uint16_t IIDXIO_TT_STATE[2]{};
    static int8_t IIDXIO_TT_DIRECTION[2]{1, 1};
    static bool IIDXIO_TT_PRESSED[2]{};
    static bool IIDXIO_TT_ALT_PRESSED[2]{};
    char IIDXIO_LED_TICKER[10] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\x00'};
    bool IIDXIO_LED_TICKER_READONLY = false;
    std::mutex IIDX_LED_TICKER_LOCK;

    tapeledutils::tape_led TAPELED_MAPPING[IIDX_TAPELED_TOTAL] = {
        { 19, Lights::StageLeftAvgR, Lights::StageLeftAvgG, Lights::StageLeftAvgB, "Stage Left" },
        { 19, Lights::StageRightAvgR, Lights::StageRightAvgG, Lights::StageRightAvgB, "Stage Right" },
        { 45, Lights::CabinetLeftAvgR, Lights::CabinetLeftAvgG, Lights::CabinetLeftAvgB, "Cabinet Left" },
        { 45, Lights::CabinetRightAvgR, Lights::CabinetRightAvgG, Lights::CabinetRightAvgB, "Cabinet Right" },
        { 21, Lights::ControlPanelUnderAvgR, Lights::ControlPanelUnderAvgG, Lights::ControlPanelUnderAvgB, "Control Panel Under" },
        { 54, Lights::CeilingLeftAvgR, Lights::CeilingLeftAvgG, Lights::CeilingLeftAvgB, "Ceiling Left" },
        { 11, Lights::TitleLeftAvgR, Lights::TitleLeftAvgG, Lights::TitleLeftAvgB, "Title Left" },
        { 11, Lights::TitleRightAvgR, Lights::TitleRightAvgG, Lights::TitleRightAvgB, "Title Right" },
        { 54, Lights::CeilingRightAvgR, Lights::CeilingRightAvgG, Lights::CeilingRightAvgB, "Ceiling Right" },
        { 17, Lights::TouchPanelLeftAvgR, Lights::TouchPanelLeftAvgG, Lights::TouchPanelLeftAvgB, "Touch Panel Left" },
        { 17, Lights::TouchPanelRightAvgR, Lights::TouchPanelRightAvgG, Lights::TouchPanelRightAvgB, "Touch Panel Right" },
        { 68, Lights::SidePanelLeftInnerAvgR, Lights::SidePanelLeftInnerAvgG, Lights::SidePanelLeftInnerAvgB, "Side Panel Left Inner" },
        { 68, Lights::SidePanelLeftOuterAvgR, Lights::SidePanelLeftOuterAvgG, Lights::SidePanelLeftOuterAvgB, "Side Panel Left Outer" },
        { 61, Lights::SidePanelLeftAvgR, Lights::SidePanelLeftAvgG, Lights::SidePanelLeftAvgB, "Side Panel Left" },
        { 68, Lights::SidePanelRightOuterAvgR, Lights::SidePanelRightOuterAvgG, Lights::SidePanelRightOuterAvgB, "Side Panel Right Outer" },
        { 68, Lights::SidePanelRightInnerAvgR, Lights::SidePanelRightInnerAvgG, Lights::SidePanelRightInnerAvgB, "Side Panel Right Inner" },
        { 61, Lights::SidePanelRightAvgR, Lights::SidePanelRightAvgG, Lights::SidePanelRightAvgB, "Side Panel Right" },
    };

    static LONG WINAPI RegOpenKeyA_hook(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult) {
        if (lpSubKey != nullptr && phkResult != nullptr) {
            if (hKey == HKEY_LOCAL_MACHINE &&
                    ASIO_DRIVER.has_value() &&
                    _stricmp(lpSubKey, "software\\asio") == 0)
            {
                *phkResult = PARENT_ASIO_REG_HANDLE;

                return RegOpenKeyA_orig(hKey, lpSubKey, &real_asio_reg_handle);
            }
        }

        return RegOpenKeyA_orig(hKey, lpSubKey, phkResult);
    }

    static LONG WINAPI RegOpenKeyExA_hook(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired,
            PHKEY phkResult)
    {
        if (lpSubKey != nullptr && phkResult != nullptr) {
            if (hKey == PARENT_ASIO_REG_HANDLE &&
                    ASIO_DRIVER.has_value() &&
                    _stricmp(lpSubKey, ORIGINAL_ASIO_DEVICE_NAME) == 0)
            {
                *phkResult = DEVICE_ASIO_REG_HANDLE;

                log_info("iidx::asio", "replacing '{}' with '{}'", lpSubKey, ASIO_DRIVER.value());
                const auto result = RegOpenKeyExA_orig(
                        real_asio_reg_handle,
                        ASIO_DRIVER.value().c_str(),
                        ulOptions,
                        samDesired,
                        &real_asio_device_reg_handle);

                if (result != ERROR_SUCCESS) {
                    log_warning(
                        "iidx::asio",
                        "failed to open registry subkey '{}', error=0x{:x}",
                        ASIO_DRIVER.value().c_str(), result);
                    log_warning(
                        "iidx::asio",
                        "due to improper ASIO setting, game will likely fail to launch; double check -iidxasio and the registry",
                        ASIO_DRIVER.value().c_str(), result);
                }

                return result;
            }
        }

        return RegOpenKeyExA_orig(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    }

    static LONG WINAPI RegEnumKeyA_hook(HKEY hKey, DWORD dwIndex, LPSTR lpName, DWORD cchName) {
        if (hKey == PARENT_ASIO_REG_HANDLE && ASIO_DRIVER.has_value()) {
            if (dwIndex == 0) {
                auto ret = RegEnumKeyA_orig(real_asio_reg_handle, dwIndex, lpName, cchName);

                if (ret == ERROR_SUCCESS && lpName != nullptr) {
                    log_info("iidx::asio", "stubbing '{}' with '{}'", lpName, ORIGINAL_ASIO_DEVICE_NAME);

                    strncpy(lpName, ORIGINAL_ASIO_DEVICE_NAME, cchName);
                }

                return ret;
            } else {
                return ERROR_NO_MORE_ITEMS;
            }
        }

        return RegEnumKeyA_orig(hKey, dwIndex, lpName, cchName);
    }

    static LONG WINAPI RegQueryValueExA_hook(HKEY hKey, LPCTSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType,
            LPBYTE lpData, LPDWORD lpcbData)
    {
        if (lpValueName != nullptr && lpData != nullptr && lpcbData != nullptr) {

            // ASIO hack
            if (hKey == DEVICE_ASIO_REG_HANDLE && ASIO_DRIVER.has_value()) {
                log_info("iidx::asio", "RegQueryValueExA({}, \"{}\")", fmt::ptr((void *) hKey), lpValueName);

                if (_stricmp(lpValueName, "Description") == 0) {
                    // newer iidx does a comparison against hardcoded string "XONAR SOUND CARD(64)" (same as sdvx)
                    // so what's in the registry must be overridden with "XONAR SOUND CARD(64)"
                    // otherwise you end up with this error: M:BMSoundLib: ASIODriver: No such driver
                    memcpy(lpData, ORIGINAL_ASIO_DEVICE_NAME, strlen(ORIGINAL_ASIO_DEVICE_NAME) + 1);

                    return ERROR_SUCCESS;
                } else {
                    hKey = real_asio_device_reg_handle;
                }
            }

            /*
            * Dirty Workaround for IO Device
            * Game gets registry object via SetupDiOpenDevRegKey, then looks up "PortName" via RegQueryValueExA
            * We ignore the first and just cheat on the second.
            */
            // check for port name lookup
            if (_stricmp(lpValueName, "PortName") == 0) {
                const char port[] = "COM2";
                memcpy(lpData, port, sizeof(port));

                return ERROR_SUCCESS;
            }
        }

        // fallback
        return RegQueryValueExA_orig(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
    }

    static LONG WINAPI RegCloseKey_hook(HKEY hKey) {
        if (hKey == PARENT_ASIO_REG_HANDLE || hKey == DEVICE_ASIO_REG_HANDLE) {
            return ERROR_SUCCESS;
        }

        return RegCloseKey_orig(hKey);
    }

    static bool log_hook(void *user, const std::string &data, logger::Style style, std::string &out) {
        if (data.empty() || data[0] != '[') {
            return false;
        }

        // get rid of the log spam
        if (data.find(" I:graphic: adapter mode ") != std::string::npos) {
            out.clear();
            return true;
        } else if (data.find(" W:afputils: CDirectX::SetRenderState ") != std::string::npos) {
            out.clear();
            return true;
        } else if (data.find("\" layer ID 0 is not layer ID.") != std::string::npos) {
            out.clear();
            return true;
        } else if (data.find(" W:touch: missing trigger:") != std::string::npos) {
            out.clear();
            return true;
        } else if (data.find("SuperstepSound: Audio device is not available") != std::string::npos) {
            deferredlogs::defer_error_messages(deferredlogs::SUPERSTEP_SOUND_ERROR_MESSAGE);
            return false;
        } else {
            return false;
        }
    }

    typedef void* (*aioIob2Bi2x_CreateWriteFirmContext_t)(unsigned int, int);
    static aioIob2Bi2x_CreateWriteFirmContext_t aioIob2Bi2x_CreateWriteFirmContext_orig = nullptr;

    static void* aioIob2Bi2x_CreateWriteFirmContext_hook(unsigned int a1, int a2) {
        if (aioIob2Bi2x_CreateWriteFirmContext_orig) {
            auto options = games::get_options(eamuse_get_game());
            if (options->at(launcher::Options::IIDXBIO2FW).value_bool()) {
                log_info("iidx", "CreateWriteFirmContext({}, {} -> 2)", a1, a2);
                return aioIob2Bi2x_CreateWriteFirmContext_orig(a1, 2);
            } else {
                log_info("iidx", "CreateWriteFirmContext({}, {})", a1, a2);
                return aioIob2Bi2x_CreateWriteFirmContext_orig(a1, a2);
            }
        }
        log_warning("iidx", "CreateWriteFirmContext == nullptr");
        return nullptr;
    }

    IIDXGame::IIDXGame() : Game("Beatmania IIDX") {
        logger::hook_add(log_hook, this);
    }

    IIDXGame::~IIDXGame() {
        logger::hook_remove(log_hook, this);
    }

    void IIDXGame::attach() {
        Game::attach();
        // IO boards
        auto options = games::get_options(eamuse_get_game());
        if (!options->at(launcher::Options::IIDXBIO2FW).value_bool()) {

            // reduce boot wait time
            hooks::sleep::init(1000, 1);

            // add old IO board
            SETUPAPI_SETTINGS settings1 {};
            settings1.class_guid[0] = 0xAE18AA60;
            settings1.class_guid[1] = 0x11D47F6A;
            settings1.class_guid[2] = 0x0100DD97;
            settings1.class_guid[3] = 0x59B92902;
            const char property1[] = "Cypress EZ-USB (2235 - EEPROM missing)";
            const char interface_detail1[] = "\\\\.\\Ezusb-0";
            memcpy(settings1.property_devicedesc, property1, sizeof(property1));
            memcpy(settings1.interface_detail, interface_detail1, sizeof(interface_detail1));
            setupapihook_init(avs::game::DLL_INSTANCE);
            setupapihook_add(settings1);

            // IIDX <25 with EZUSB input device
            devicehook_init();
            devicehook_add(new EZUSBHandle());

            // add new BIO2 I/O board
            SETUPAPI_SETTINGS settings2 {};
            settings2.class_guid[0] = 0x4D36E978;
            settings2.class_guid[1] = 0x11CEE325;
            settings2.class_guid[2] = 0x0008C1BF;
            settings2.class_guid[3] = 0x1803E12B;
            const char property2[] = "BIO2(VIDEO)";
            const char interface_detail2[] = "COM2";
            memcpy(settings2.property_devicedesc, property2, sizeof(property2));
            memcpy(settings2.interface_detail, interface_detail2, sizeof(interface_detail2));
            setupapihook_add(settings2);

            // IIDX 25-27 BIO2 BI2A input device
            devicehook_add(new IIDXFMSerialHandle());
        }

        // check for new I/O DLL
        auto aio = libutils::try_library("libaio.dll");
        if (aio != nullptr) {

            // check TDJ mode
            TDJ_MODE |= fileutils::text_read("C:\\000rom.txt") == "TDJ-JA";
            TDJ_MODE |= fileutils::text_read("D:\\001rom.txt") == "TDJ";

            // force TDJ mode
            if (TDJ_MODE) {

                // ensure game starts in the desired mode
                hooks::avs::set_rom("/c_drv/000rom.txt", "TDJ-JA");
                hooks::avs::set_rom("/d_drv/001rom.txt", "TDJ");

                // need to hook `avs2-core.dll` so AVS win32fs operations go through rom hook
                devicehook_init(avs::core::DLL_INSTANCE);

                if (!NATIVE_TOUCH) {
                    wintouchemu::FORCE = true;
                    wintouchemu::INJECT_MOUSE_AS_WM_TOUCH = true;
                    wintouchemu::hook_title_ends("beatmania IIDX", "main", avs::game::DLL_INSTANCE);
                }

                // prevent crash on TDJ mode without correct DLL
                if (!GetModuleHandle("nvcuda.dll")) {
                    DISABLE_CAMS = true;
                }
            }

            // insert BI2X hooks
            bi2x_hook_init();

            // add card readers
            devicehook_init(aio);
            devicehook_add(new acioemu::ACIOHandle(L"COM1"));

            // firmware upgrade hook
            if (options->at(launcher::Options::IIDXBIO2FW).value_bool()) {
                aioIob2Bi2x_CreateWriteFirmContext_orig = detour::iat(
                        "aioIob2Bi2x_CreateWriteFirmContext",
                        aioIob2Bi2x_CreateWriteFirmContext_hook);
                //devicehook_add(new MITMHandle(L"#vid_1ccf&pid_8050", "", true));
            } else {

                /*
                // add the BIO2 I/O board (with different firmware)
                SETUPAPI_SETTINGS settings {};
                settings.class_guid[0] = 0x0;
                settings.class_guid[1] = 0x0;
                settings.class_guid[2] = 0x0;
                settings.class_guid[3] = 0x0;
                const char property[] = "1CCF(8050)_000";
                const char property_hardwareid[] = "USB\\VID_1CCF&PID_8050&MI_00\\000";
                const char interface_detail[] = "COM3";
                memcpy(settings.property_devicedesc, property, sizeof(property));
                memcpy(settings.property_hardwareid, property_hardwareid, sizeof(property_hardwareid));
                memcpy(settings.interface_detail, interface_detail, sizeof(interface_detail));
                setupapihook_init(aio);
                setupapihook_add(settings);

                // IIDX 27 BIO2 BI2X input devce
                devicehook_add(new BI2XSerialHandle());
                */
            }
        }

        if (hooks::audio::ENABLED) {
            apply_audio_hacks();
        } else {
            log_warning("iidx", "skipping audio hooks; disabled by user");
        }

        // ASIO device hook
        RegCloseKey_orig = detour::iat_try(
                "RegCloseKey", RegCloseKey_hook, avs::game::DLL_INSTANCE);
        RegEnumKeyA_orig = detour::iat_try(
                "RegEnumKeyA", RegEnumKeyA_hook, avs::game::DLL_INSTANCE);
        RegOpenKeyA_orig = detour::iat_try(
                "RegOpenKeyA", RegOpenKeyA_hook, avs::game::DLL_INSTANCE);
        RegOpenKeyExA_orig = detour::iat_try(
                "RegOpenKeyExA", RegOpenKeyExA_hook, avs::game::DLL_INSTANCE);

        // IO device workaround
        RegQueryValueExA_orig = detour::iat_try(
                "RegQueryValueExA", RegQueryValueExA_hook, avs::game::DLL_INSTANCE);

        // check if cam hook should be enabled
        if (!DISABLE_CAMS.has_value()) {
            log_fatal("iidx", "assertion failure - DISABLE_CAMS not set during attach");
        }
        if (!DISABLE_CAMS) {
            init_legacy_camera_hook(FLIP_CAMS);
        }

        // init cfgmgr32 hooks
        cfgmgr32hook_init(avs::game::DLL_INSTANCE);
    }

    void IIDXGame::pre_attach() {
        Game::pre_attach();
        auto options = games::get_options(eamuse_get_game());

        // environment variables must be set before the DLL is loaded as the VC++ runtime copies all
        // environment variables at startup
        if (SCREEN_MODE.has_value()) {
            log_misc("iidx", "SCREEN_MODE env var set to {}", SCREEN_MODE.value().c_str());
            SetEnvironmentVariable("SCREEN_MODE", SCREEN_MODE.value().c_str());
        }

        // check for cab camera access for the second time (first time was in launcher.cpp)
        // this time, we are inside -iidx module hook, which means the user is likely NOT on a cab
        // therefore, start with cams OFF by default, and allow user to forcibly override to ON
        if (!games::iidx::DISABLE_CAMS.has_value()) {
            games::iidx::DISABLE_CAMS = true;
            if (options->at(launcher::Options::IIDXCabCamAccess).is_active() &&
                options->at(launcher::Options::IIDXCabCamAccess).value_text() == "on") {
                games::iidx::DISABLE_CAMS = false;
            }
            if (games::iidx::DISABLE_CAMS.value()) {
                log_misc("iidx", "CONNECT_CAMERA env var set to 0");
                SetEnvironmentVariable("CONNECT_CAMERA", "0");
            }
        }

        // windowed subscreen, enabled by default, unless turned off by user
        if (GRAPHICS_WINDOWED && !options->at(launcher::Options::spice2x_IIDXNoSub).value_bool()) {
            GRAPHICS_IIDX_WSUB = true;
        }

#ifdef SPICE64
        this->detect_sound_output_device();
#endif

        // check bad model name
        if (!cfg::CONFIGURATOR_STANDALONE && avs::game::is_model("TDJ")) {
            log_warning(
                "iidx",
                "BAD MODEL NAME ERROR\n\n\n"
                "!!! model name set to TDJ, this is WRONG and will break your game !!!\n"
                "!!!                                                               !!!\n"
                "!!! If you are trying to boot IIDX with Lightning Model mode,     !!!\n"
                "!!! please do the following instead:                              !!!\n"
                "!!!                                                               !!!\n"
                "!!! Revert your changes to XML file so it says                    !!!\n"
                "!!!     <model __type=\"str\">LDJ</model>                           !!!\n" 
                "!!!                                                               !!!\n"
                "!!! In SpiceCfg, enable 'IIDX TDJ Mode' or provide -iidxtdj flag  !!!\n"
                "!!! in command line                                               !!!\n"
                "!!!                                                               !!!\n"
                "!!! Apply any applicable settings / patches / hex edits           !!!\n"
                "!!!                                                               !!!\n"
                "!!! model name set to TDJ, this is WRONG and will break your game !!!\n\n\n"
                );

            log_fatal("iidx", "BAD MODEL NAME ERROR - TDJ specified, must be LDJ instead");
        }
    }

    void IIDXGame::detach() {
        Game::detach();

        devicehook_dispose();
    }

    void IIDXGame::detect_sound_output_device() {
        // if the user specified a value (other than auto), use it as the environment var
        // probably "wasapi" or "asio", but it's not explicitly checked here for forward compat
        if (SOUND_OUTPUT_DEVICE.has_value() && SOUND_OUTPUT_DEVICE.value() != "auto") {
            log_info(
                "iidx",
                "using user-supplied \"{}\" for SOUND_OUTPUT_DEVICE",
                SOUND_OUTPUT_DEVICE.value());
            SetEnvironmentVariable("SOUND_OUTPUT_DEVICE", SOUND_OUTPUT_DEVICE.value().c_str());
            SOUND_OUTPUT_DEVICE_IN_EFFECT = SOUND_OUTPUT_DEVICE;
            return;
        }

        // automatic detection
        bool use_asio = false;
        log_misc("iidx", "auto-detect SOUND_OUTPUT_DEVICE...");
        if (ASIO_DRIVER.has_value()) {
            log_misc(
                "iidx",
                "-iidxasio is set to \"{}\", use asio for SOUND_OUTPUT_DEVICE",
                ASIO_DRIVER.value());
            use_asio = true;
        } else {
            HKEY subkey;
            LSTATUS result;
            result = RegOpenKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO\\XONAR SOUND CARD(64)", &subkey);
            if (result == ERROR_SUCCESS) {
                RegCloseKey(subkey);
                use_asio = true;
                log_misc(
                    "iidx",
                    "found HKLM\\SOFTWARE\\ASIO\\XONAR SOUND CARD(64), using asio for SOUND_OUTPUT_DEVICE");
            }
        }

        const char* device = "wasapi";
        if (use_asio) {
            device = "asio";
        }
        log_info("iidx", "SOUND_OUTPUT_DEVICE set to {}", device);
        SetEnvironmentVariable("SOUND_OUTPUT_DEVICE", device);
        SOUND_OUTPUT_DEVICE_IN_EFFECT = device;
    }

    uint32_t get_pad() {
        uint32_t pad = 0;

        // get buttons
        auto &buttons = get_buttons();

        // player 1 buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_1)))
            pad |= 1 << 0x08;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_2)))
            pad |= 1 << 0x09;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_3)))
            pad |= 1 << 0x0A;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_4)))
            pad |= 1 << 0x0B;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_5)))
            pad |= 1 << 0x0C;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_6)))
            pad |= 1 << 0x0D;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_7)))
            pad |= 1 << 0x0E;

        // player 2 buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_1)))
            pad |= 1 << 0x0F;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_2)))
            pad |= 1 << 0x10;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_3)))
            pad |= 1 << 0x11;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_4)))
            pad |= 1 << 0x12;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_5)))
            pad |= 1 << 0x13;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_6)))
            pad |= 1 << 0x14;
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_7)))
            pad |= 1 << 0x15;

        // player 1 start
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_Start)))
            pad |= 1 << 0x18;

        // player 2 start
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_Start)))
            pad |= 1 << 0x19;

        // VEFX
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::VEFX)))
            pad |= 1 << 0x1A;

        // EFFECT
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Effect)))
            pad |= 1 << 0x1B;

        // test
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Test)))
            pad |= 1 << 0x1C;

        // service
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Service)))
            pad |= 1 << 0x1D;

        return ~(pad & 0xFFFFFF00);
    }

    void write_lamp(uint16_t lamp) {

        // mapping
        static const size_t mapping[] = {
                Lights::P1_1,
                Lights::P1_2,
                Lights::P1_3,
                Lights::P1_4,
                Lights::P1_5,
                Lights::P1_6,
                Lights::P1_7,
                Lights::P2_1,
                Lights::P2_2,
                Lights::P2_3,
                Lights::P2_4,
                Lights::P2_5,
                Lights::P2_6,
                Lights::P2_7,
        };

        // get lights
        auto &lights = get_lights();

        // bit scan
        for (int i = 0; i < 14; i++) {
            float value = (lamp & (1 << i)) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
        }
    }

    void write_led(uint8_t led) {

        // mapping
        static const size_t mapping[] = {
                Lights::P1_Start,
                Lights::P2_Start,
                Lights::VEFX,
                Lights::Effect,
        };

        // get lights
        auto &lights = get_lights();

        // bit scan
        for (int i = 0; i < 4; i++) {
            auto value = (led & (1 << i)) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
        }
    }

    void write_top_lamp(uint8_t top_lamp) {

        // mapping
        static const size_t mapping[] = {
                Lights::SpotLight1,
                Lights::SpotLight2,
                Lights::SpotLight3,
                Lights::SpotLight4,
                Lights::SpotLight5,
                Lights::SpotLight6,
                Lights::SpotLight7,
                Lights::SpotLight8,
        };

        // get lights
        auto &lights = get_lights();

        // bit scan
        for (int i = 0; i < 8; i++) {
            auto value = (top_lamp & (1 << i)) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
        }
    }

    void write_top_neon(uint8_t top_neon) {

        // get lights
        auto &lights = get_lights();

        // write value
        auto value = top_neon > 0 ? 1.f : 0.f;
        GameAPI::Lights::writeLight(RI_MGR, lights.at(Lights::NeonLamp), value);
    }

    unsigned char get_tt(int player, bool slow) {

        // check change value for high/low precision
        uint16_t change =
            slow ? (uint16_t) (DIGITAL_TT_SENS / 4) : (uint16_t) DIGITAL_TT_SENS;

        // check player number
        if (player > 1)
            return 0;

        // get buttons
        auto &buttons = get_buttons();
        bool ttp = GameAPI::Buttons::getState(RI_MGR, buttons.at(
                player != 0 ? Buttons::P2_TTPlus : Buttons::P1_TTPlus));
        bool ttm = GameAPI::Buttons::getState(RI_MGR, buttons.at(
                player != 0 ? Buttons::P2_TTMinus : Buttons::P1_TTMinus));

        bool ttpm = GameAPI::Buttons::getState(RI_MGR, buttons.at(
                player != 0 ? Buttons::P2_TTPlusMinus : Buttons::P1_TTPlusMinus));
        bool ttpm_alt = GameAPI::Buttons::getState(RI_MGR, buttons.at(
                player != 0 ? Buttons::P2_TTPlusMinusAlt : Buttons::P1_TTPlusMinusAlt));

        // TT+
        if (ttp)
            IIDXIO_TT_STATE[player] += change;

        // TT-
        if (ttm)
            IIDXIO_TT_STATE[player] -= change;

        // TT+/-
        bool ttpm_rising_edge = !IIDXIO_TT_PRESSED[player] && ttpm;
        bool ttpm_alt_rising_edge = !IIDXIO_TT_ALT_PRESSED[player] && ttpm_alt;
        if (ttpm_rising_edge || ttpm_alt_rising_edge) {
            IIDXIO_TT_DIRECTION[player] *= -1;
        }
        if (ttpm || ttpm_alt) {
            IIDXIO_TT_STATE[player] += (change * IIDXIO_TT_DIRECTION[player]);
        }
        IIDXIO_TT_PRESSED[player] = ttpm;
        IIDXIO_TT_ALT_PRESSED[player] = ttpm_alt;

        // raw input
        auto &analogs = get_analogs();
        auto &analog = analogs[player != 0 ? Analogs::TT_P2 : Analogs::TT_P1];
        auto ret_value = IIDXIO_TT_STATE[player];
        if (analog.isSet()) {
            ret_value = IIDXIO_TT_STATE[player];
            ret_value += (uint16_t) (GameAPI::Analogs::getState(RI_MGR, analog) * 1023.999f);
        }

        // return higher 8 bit
        return (uint8_t) (ret_value >> 2);
    }

    unsigned char get_slider(uint8_t slider) {

        // check slide
        if (slider > 4)
            return 0;

        // get analog
        auto &analogs = get_analogs();
        Analog *analog = nullptr;
        switch (slider) {
            case 0:
                analog = &analogs.at(Analogs::VEFX);
                break;
            case 1:
                analog = &analogs.at(Analogs::LowEQ);
                break;
            case 2:
                analog = &analogs.at(Analogs::HiEQ);
                break;
            case 3:
                analog = &analogs.at(Analogs::Filter);
                break;
            case 4:
                analog = &analogs.at(Analogs::PlayVolume);
                break;
            default:
                break;
        }

        // if not set return max value
        if (!analog || !analog->isSet()) {
            return 0xF;
        }

        // return slide
        return (unsigned char) (GameAPI::Analogs::getState(RI_MGR, *analog) * 15.999f);
    }

    const char* get_16seg() {
        return IIDXIO_LED_TICKER;
    }

    bool is_tdj_fhd() {
        return TDJ_MODE && avs::game::is_ext(2022101900, MAXINT);
    }

    void apply_audio_hacks() {

#ifdef SPICE64

        const auto has_XONAR_SOUND_CARD = find_pattern(
                avs::game::DLL_INSTANCE,
                "584F4E415220534F554E442043415244", // XONAR SOUND CARD
                "XXXXXXXXXXXXXXXX",
                0,
                0);

        const auto has_SOUND_OUTPUT_DEVICE = find_pattern(
                avs::game::DLL_INSTANCE,
                "534F554E445F4F55545055545F444556494345", // SOUND_OUTPUT_DEVICE
                "XXXXXXXXXXXXXXXXXXX",
                0,
                0);

        // attempt to detect ASIO support
        // <=24 : 32-bit only
        // 25-26: has neither (no patch needed - WASAPI Exclusive by default)
        // 27-30: has both (envvar will be respected, ASIO or WASAPI)
        // 31+: only has XONAR (ASIO by default, signature patch can be used to force WASAPI - for now)

        if (!has_SOUND_OUTPUT_DEVICE && !has_XONAR_SOUND_CARD) {
            // iidx 25-26
            log_info("iidx", "This game only uses WASAPI audio engine");
            return;
        }

        if (has_SOUND_OUTPUT_DEVICE && has_XONAR_SOUND_CARD) {
            // iidx 27-30
            log_info("iidx", "This game accepts SOUND_OUTPUT_DEVICE environment variable");
            return;
        }

        log_info("iidx", "This game supports ASIO but does not accept SOUND_OUTPUT_DEVICE environment variable");

        // patch game to force wasapi
        if (SOUND_OUTPUT_DEVICE_IN_EFFECT.has_value() && SOUND_OUTPUT_DEVICE_IN_EFFECT.value() == "wasapi") {
            intptr_t result = replace_pattern(
                avs::game::DLL_INSTANCE,
                "FF5008E8??????FF83780803740D",
                "??????BB00000000EB169090????",
                0, 0);

            if (result == 0) {
                log_warning(
                    "iidx",
                    "Failed to force WASAPI as audio engine using signature matching. "
                    "Unless patches are applied, game will default to ASIO");
            } else {
                log_info(
                    "iidx",
                    "Successfully forced WASAPI as audio engine using signature matching @ 0x{:x}.",
                    result);
            }
        } else {
            log_info("iidx", "Not applying force wasapi patch; game will use ASIO");
        }

        // patch iidx32+ for asio compatibility
        // only do this if NOT wasapi (as opposed to checking if it's asio)
        // the patch is only really needed for (some) non-XONAR devices but since people sometimes disguise 
        // other devices as a XONAR, don't check for the exact string (common ASIO workaround for INF)
        if (avs::game::is_ext(2024090100, MAXINT) &&
            !(SOUND_OUTPUT_DEVICE_IN_EFFECT.has_value() && SOUND_OUTPUT_DEVICE_IN_EFFECT.value() == "wasapi")) {
                
            // in iidx32 final:
            // ff 50 08      call   QWORD PTR [rax+0x8]     ; ASIO instance AddRef
            // 48 8b 4b 08   mov    rcx,QWORD PTR [rbx+0x8]
            // 48 8b 01      mov    rax,QWORD PTR [rcx]
            // ff 50 08      call   QWORD PTR [rax+0x8]     ; ASIO instance AddRef

            intptr_t result = replace_pattern(
                avs::game::DLL_INSTANCE,
                "FF50????????????????FF50??4533C94533C0418D51",
                "????????????????????909090??????????????????",
                0, 0);

            if (result == 0) {
                log_warning(
                    "iidx",
                    "Failed to apply ASIO compatibility fix for iidx32+. "
                    "Unless patches are applied, your ASIO device ({}) may hang or fail to work",
                    ASIO_DRIVER->c_str());
            } else {
                log_info(
                    "iidx",
                    "Successfully applied ASIO compatibility fix for iidx32+ using signature matching @ 0x{:x}.",
                    result);
            }
        }

#endif

    }
}
