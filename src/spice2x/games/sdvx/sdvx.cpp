#include "sdvx.h"

#include <external/robin_hood.h>

#include "avs/game.h"
#include "games/shared/lcdhandle.h"
#include "hooks/audio/audio.h"
#include "hooks/graphics/graphics.h"
#include "hooks/devicehook.h"
#include "hooks/libraryhook.h"
#include "hooks/graphics/nvapi_hook.h"
#include "hooks/powrprof.h"
#include "hooks/sleephook.h"
#include "hooks/winuser.h"
#include "touch/touch.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/sigscan.h"
#include "util/libutils.h"
#include "misc/wintouchemu.h"
#include "misc/eamuse.h"
#include "bi2x_hook.h"
#include "camera.h"
#include "io.h"
#include "acioemu/handle.h"
#include "cfg/configurator.h"

static decltype(RegCloseKey) *RegCloseKey_orig = nullptr;
static decltype(RegEnumKeyA) *RegEnumKeyA_orig = nullptr;
static decltype(RegOpenKeyA) *RegOpenKeyA_orig = nullptr;
static decltype(RegOpenKeyExA) *RegOpenKeyExA_orig = nullptr;
static decltype(RegQueryValueExA) *RegQueryValueExA_orig = nullptr;

namespace games::sdvx {

    // constants
    const HKEY PARENT_ASIO_REG_HANDLE = reinterpret_cast<HKEY>(0x3001);
    const HKEY DEVICE_ASIO_REG_HANDLE = reinterpret_cast<HKEY>(0x3002);
    const char *ORIGINAL_ASIO_DEVICE_NAME = "XONAR SOUND CARD(64)";

    // settings
    bool NATIVETOUCH = false;
    uint8_t DIGITAL_KNOB_SENS = 16;
    SdvxOverlayPosition OVERLAY_POS = SDVX_OVERLAY_BOTTOM;
    bool ENABLE_COM_PORT_SCAN_HOOK = false;

    std::optional<std::string> SOUND_OUTPUT_DEVICE = std::nullopt;
    std::optional<std::string> ASIO_DRIVER = std::nullopt;

    // states
    static HKEY real_asio_reg_handle = nullptr;
    static HKEY real_asio_device_reg_handle = nullptr;

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
        // ASIO hook
        if (lpSubKey != nullptr && phkResult != nullptr) {
            if (hKey == PARENT_ASIO_REG_HANDLE &&
                    ASIO_DRIVER.has_value() &&
                    _stricmp(lpSubKey, ORIGINAL_ASIO_DEVICE_NAME) == 0)
            {
                *phkResult = DEVICE_ASIO_REG_HANDLE;

                log_info("sdvx::asio", "replacing '{}' with '{}'", lpSubKey, ASIO_DRIVER.value());
                const auto result = RegOpenKeyExA_orig(
                        real_asio_reg_handle,
                        ASIO_DRIVER.value().c_str(),
                        ulOptions,
                        samDesired,
                        &real_asio_device_reg_handle);

                if (result != ERROR_SUCCESS) {
                    log_warning(
                        "sdvx::asio",
                        "failed to open registry subkey '{}', error=0x{:x}",
                        ASIO_DRIVER.value().c_str(), result);
                    log_warning(
                        "sdvx::asio",
                        "due to improper ASIO setting, game will likely fall back to WASAPI; double check -sdvxasio and the registry",
                        ASIO_DRIVER.value().c_str(), result);
                }

                return result;
            }
        }

        // COM hook
        if (ENABLE_COM_PORT_SCAN_HOOK &&
            lpSubKey != nullptr && phkResult != nullptr &&
	        _stricmp(lpSubKey, "HARDWARE\\DEVICEMAP\\SERIALCOMM") == 0) {
            log_info("sdvx::io", "failing HKLM\\HARDWARE\\DEVICEMAP\\SERIALCOMM to force COM1 ICCA");
            return 2; //ERROR_FILE_NOT_FOUND
        }

        return RegOpenKeyExA_orig(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    }

    static LONG WINAPI RegEnumKeyA_hook(HKEY hKey, DWORD dwIndex, LPSTR lpName, DWORD cchName) {
        if (hKey == PARENT_ASIO_REG_HANDLE && ASIO_DRIVER.has_value()) {
            if (dwIndex == 0) {
                auto ret = RegEnumKeyA_orig(real_asio_reg_handle, dwIndex, lpName, cchName);

                if (ret == ERROR_SUCCESS && lpName != nullptr) {
                    log_info("sdvx::asio", "stubbing '{}' with '{}'", lpName, ORIGINAL_ASIO_DEVICE_NAME);

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
            if (hKey == DEVICE_ASIO_REG_HANDLE && ASIO_DRIVER.has_value()) {
                log_info("sdvx::asio", "RegQueryValueExA({}, \"{}\")", fmt::ptr((void *) hKey), lpValueName);

                if (_stricmp(lpValueName, "Description") == 0) {
                    // sdvx does a comparison against hardcoded string "XONAR SOUND CARD(64)" (same as iidx31)
                    // so what's in the registry must be overridden with "XONAR SOUND CARD(64)"
                    // otherwise you end up with this error: M:BMSoundLib: ASIODriver: No such driver
                    memcpy(lpData, ORIGINAL_ASIO_DEVICE_NAME, strlen(ORIGINAL_ASIO_DEVICE_NAME) + 1);

                    return ERROR_SUCCESS;
                } else {
                    hKey = real_asio_device_reg_handle;
                }
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

    SDVXGame::SDVXGame() : Game("Sound Voltex") {
    }

    static LPWSTR __stdcall GetCommandLineW_hook() {
        static std::wstring lp_args = L"bootstrap.exe prop\\bootstrap.xml";
        return lp_args.data();
    }

#ifdef SPICE64
    static bool sdvx64_spam_remover(void *user, const std::string &data, logger::Style style, std::string &out) {
        if (data.empty() || data[0] != '[') {
            return false;
        }
        if (data.find("W:afpu-package: XE592acd000040 texture id invalid") != std::string::npos) {
            out = "";
            return true;
        }
        if (data.find("W:afpu-package: XE592acd000042 texture id invalid") != std::string::npos) {
            out = "";
            return true;
        }
        if (data.find("W:CTexture: no such texture: id 0") != std::string::npos) {
            out = "";
            return true;
        }
        if (data.find("M:autoDj: DEF phrase ") != std::string::npos) {
            out = "";
            return true;
        }
        if (data.find("W:afp-access: afp_mc_deep_goto_play frame no error") != std::string::npos) {
            out = "";
            return true;
        }
        if (data.find("W:afputils: CDirectX::SetRenderState") != std::string::npos) {
            out = "";
            return true;
        }
        if (data.find("W:CameraTexture: Camera error was detected. (err,detail) = (0,0)") != std::string::npos) {
            out = "";
            return true;
        }
        // M:AppConfig: [env/APPDATA]=C:\Users\username\AppData\Roaming
        if (data.find("M:AppConfig: [env/") != std::string::npos) {
            out = "";
            return true;
        }
        return false;
    }

    typedef void **(__fastcall *volume_set_t)(uint64_t, uint64_t, uint64_t);
    static volume_set_t volume_set_orig = nullptr;
    static void **__fastcall volume_set_hook(uint64_t vol_sound, uint64_t vol_woofer, uint64_t vol_headphone) {

        // volume level conversion tables
        static uint8_t SOUND_VOLUMES[] = {
                4, 55, 57, 59, 61, 63, 65, 67, 69, 71,
                73, 75, 77, 78, 79, 80, 81, 82, 83, 84,
                85, 86, 87, 88, 89, 90, 91, 92, 93, 95, 96,
        };
        static uint8_t WOOFER_VOLUMES[] = {
                4, 70, 72, 73, 74, 75, 76, 77, 79, 80,
                81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
                91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 100,
        };
        static uint8_t HEADPHONE_VOLUMES[] = {
                4, 60, 62, 64, 66, 68, 70, 72, 76, 78,
                80, 82, 83, 84, 85, 86, 87, 88, 89, 90,
                91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 100,
        };

        // apply volumes
        auto &format = hooks::audio::FORMAT.Format;
        auto &lights = games::sdvx::get_lights();
        if (format.nChannels == 6 || vol_sound != 30) {
            if (vol_sound < std::size(SOUND_VOLUMES) && vol_sound != 30) {
                float value = (float) SOUND_VOLUMES[vol_sound] * 0.01f;
                GameAPI::Lights::writeLight(RI_MGR, lights[Lights::VOLUME_SOUND], value);
            }
        }
        if (vol_woofer < std::size(WOOFER_VOLUMES)) {
            float value = (float) WOOFER_VOLUMES[vol_woofer] * 0.01f;
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::VOLUME_WOOFER], value);
        }
        if (vol_headphone < std::size(HEADPHONE_VOLUMES)) {
            float value = (float) HEADPHONE_VOLUMES[vol_headphone] * 0.01f;
            GameAPI::Lights::writeLight(RI_MGR, lights[Lights::VOLUME_HEADPHONE], value);
        }

        // call original function to set volumes for the 6ch mode
        return volume_set_orig(format.nChannels == 6 ? vol_sound : 30, vol_woofer, vol_headphone);
    }
#endif

    void SDVXGame::pre_attach() {
        // for whatever reason, sdvx latches onto cards for much longer than other games
        // needed because the game waits forever on the game over screen until a card is not detected
        AUTO_INSERT_CARD_COOLDOWN = 15.f;
        // check bad model name
        if (!cfg::CONFIGURATOR_STANDALONE && avs::game::is_model("UFC")) {
            log_fatal(
                "sdvx",
                "BAD MODEL NAME ERROR\n\n\n"
                "!!! model name set to UFC, this is WRONG and will break your game !!!\n"
                "!!!                                                               !!!\n"
                "!!! If you are trying to boot Valkyrie Model,                     !!!\n"
                "!!! change <spec> from F to G.                                    !!!\n"
                "!!!                                                               !!!\n"
                "!!! model name set to UFC, this is WRONG and will break your game !!!\n\n\n"
                );
        }   
    }

    void SDVXGame::attach() {
        Game::attach();

#ifdef SPICE64 // SDVX5+ specific code
        bool isValkyrieCabinetMode = avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H';

        // LCD handle
        if (!isValkyrieCabinetMode) {
            devicehook_init();
            devicehook_add(new games::shared::LCDHandle());
        }
#else
        devicehook_init();
        devicehook_add(new games::shared::LCDHandle());
#endif
        hooks::sleep::init(1000, 1);

        // hooks for chinese SDVX
        if (libutils::try_module("unisintr.dll")) {
            detour::iat_try("GetCommandLineW", GetCommandLineW_hook);

            // skip 30 second timeout after NETWORK DEVICE check
            replace_pattern(
                    avs::game::DLL_INSTANCE,
                    "89F528003D????0000",
                    "89F528003D01000000",
                    0, 0);
        }

#ifdef SPICE64 // SDVX5+ specific code

        // check for new I/O DLL
        auto aio = libutils::try_library("libaio.dll");
        if (aio != nullptr) {

            // enable 9on12 for AMD
            if (!libutils::try_library("nvapi64.dll")) {
                log_info(
                    "sdvx",
                    "nvapi64.dll not found; for non-NVIDIA GPUs, requesting 9on12 to be enabled");
                GRAPHICS_9_ON_12_REQUESTED_BY_GAME = true;
            } else {
                // don't let nvapi mess with display settings
                nvapi_hook::initialize(avs::game::DLL_INSTANCE);
            }

            // check for Valkyrie cabinet mode
            if (isValkyrieCabinetMode) {
                // hook touch window
                // in windowed mode, game can accept mouse input on the second screen
                if (!NATIVETOUCH && !GRAPHICS_WINDOWED) {
                    wintouchemu::FORCE = true;
                    wintouchemu::INJECT_MOUSE_AS_WM_TOUCH = true;
                    wintouchemu::hook_title_ends(
                            "SOUND VOLTEX",
                            "Main Screen",
                            avs::game::DLL_INSTANCE);
                }

                // insert BI2X hooks
                bi2x_hook_init();

                // add card readers
                devicehook_init(aio);
                devicehook_add(new acioemu::ACIOHandle(L"COM1"));

                // this is needed because on some newer versions of SDVX6, soundvoltex.dll will open
                // HKLM\HARDWARE\DEVICEMAP\SERIALCOMM, go through some of the keys, and depending on
                // what is present, pick a port other than COM1 (seemingly the highest port
                // available). We want the game to pick COM1 still, so a workaround is needed to
                // fool the game.
                ENABLE_COM_PORT_SCAN_HOOK = true;
            }
        }
#endif

        // ASIO device hook
        RegCloseKey_orig = detour::iat_try(
                "RegCloseKey", RegCloseKey_hook, avs::game::DLL_INSTANCE);
        RegEnumKeyA_orig = detour::iat_try(
                "RegEnumKeyA", RegEnumKeyA_hook, avs::game::DLL_INSTANCE);
        RegOpenKeyA_orig = detour::iat_try(
                "RegOpenKeyA", RegOpenKeyA_hook, avs::game::DLL_INSTANCE);
        RegOpenKeyExA_orig = detour::iat_try(
                "RegOpenKeyExA", RegOpenKeyExA_hook, avs::game::DLL_INSTANCE);
        RegQueryValueExA_orig = detour::iat_try(
                "RegQueryValueExA", RegQueryValueExA_hook, avs::game::DLL_INSTANCE);

#ifdef SPICE64
        powrprof_hook_init(avs::game::DLL_INSTANCE);
        winuser_hook_init(avs::game::DLL_INSTANCE);

        // hook camera
        camera_init();

        // RGB CAMERA error ignore for SDVX5
        // SDVX5: boot sequence triggers camera error if camera is not detected
        // SDVX6: boots fine, but game title screen in attract loop will have camera error (cosmetic only)
        if (replace_pattern(
                avs::game::DLL_INSTANCE,
                "418D480484C074218D51FD",
                "????????????9090??????",
                0, 0)) {
            log_info("sdvx", "applied camera error patch (sdvx5)");
        }

        // remove log spam
        logger::hook_add(sdvx64_spam_remover, nullptr);

#endif
    }

    void SDVXGame::post_attach() {
        Game::post_attach();

#ifdef SPICE64 // SDVX5+ specific code

        /*
         * Volume Hook
         *
         * How to find the correct RVA:
         *
         * Method 1 (older versions):
         * Search for byte sequence 48 8B C4 48 81 EC 88 00 00 00 80 3D
         *
         * Method 2 (older versions):
         * 1. search for ac_io_bi2a_set_amp_volume
         * 2. move one function up (function where it does some calculations, that's ours)
         * 3. take the *file offset* of the *first* instruction of this function
         *
         * Method 3:
         * Search for the function with the 3 arguments which is being called from the sound options.
         * It is pretty obvious which one it is because it checks all 3 args to be <= 30.
         */
        static const robin_hood::unordered_map<std::string, intptr_t> VOLUME_HOOKS {
                { "2019100800", 0x414ED0 },
                { "2020011500", 0x417090 },
                { "2020022700", 0x4281A0 },
                { "2020122200", 0x40C030 },
                { "2021042800", 0x096EB0 },
                { "2021051802", 0x097930 },
                { "2021083100", 0x096E20 },
                { "2021102000", 0x097230 },
                { "2021121400", 0x09AD00 },
        };

        bool volume_hook_found = false;
        for (auto &[datecode, rva] : VOLUME_HOOKS) {
            if (avs::game::is_ext(datecode.c_str())) {

                // calculate target RVA
                auto volume_set_rva = libutils::offset2rva(MODULE_PATH / avs::game::DLL_NAME, rva);
                if (volume_set_rva == -1) {
                    log_info("sdvx", "could not apply volume hook patch (convert rva {})", rva);
                    break;
                }

                // convert RVA to real target
                auto *volume_set_ptr = reinterpret_cast<uint16_t *>(
                        reinterpret_cast<intptr_t>(avs::game::DLL_INSTANCE) + volume_set_rva);
                if (volume_set_ptr[0] != 0x8B48) {
                    log_info("sdvx", "could not apply volume hook patch (invalid target)", rva);
                    break;
                }

                // insert trampoline
                if (!detour::trampoline(
                            reinterpret_cast<volume_set_t>(volume_set_ptr),
                            volume_set_hook,
                            &volume_set_orig))
                {
                    log_info("sdvx", "could not apply volume hook patch (insert trampoline)", rva);
                }

                // success
                volume_hook_found = true;
                break;
            }
        }

        // check if version not found
        if (!volume_hook_found) {
            log_info("sdvx", "volume hook unavailable for this game version");

            // set volumes to sdvx 4 defaults
            auto &lights = games::sdvx::get_lights();
            GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_SOUND],
                    (100 - 15) / 100.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_HEADPHONE],
                    (100 - 9) / 100.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_EXTERNAL],
                    (100 - 96) / 100.f);
            GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_WOOFER],
                    (100 - 9) / 100.f);
        }
#endif
    }

    void SDVXGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}
