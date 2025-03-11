#include "avs/game.h"
#include "popn.h"
#include <cstdint>
#include <cstring>
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/utils.h"
#include "cfg/button.h"
#include "cfg/api.h"
#include "hooks/sleephook.h"
#include "launcher/launcher.h"
#include "launcher/logger.h"
#include "misc/eamuse.h"
#include "io.h"

namespace games::popn {

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

        // only needed for older versions (19-21, inclusive) but create them anyway since 21 and above are all M39
        fileutils::dir_create_recursive_log("popn", "dev\\raw\\bookkeeping");
        fileutils::dir_create_recursive_log("popn", "dev\\raw\\ranking");
        fileutils::dir_create_recursive_log("popn", "dev\\raw\\settings");

        // load without resolving references
        // makes game not trigger DLLMain which results in an error due to missing EZUSB device
        LoadLibraryExW((MODULE_PATH / "ezusb.dll").c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    }

    void POPNGame::attach() {
        Game::attach();

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
    }
}
