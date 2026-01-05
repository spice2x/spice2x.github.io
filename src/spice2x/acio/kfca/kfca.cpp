#include "kfca.h"

#include "avs/game.h"
#include "games/bs/io.h"
#include "games/nost/io.h"
#include "games/scotto/io.h"
#include "games/sdvx/sdvx.h"
#include "games/sdvx/io.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/socd_cleaner.h"
#include "util/time.h"
#include "util/utils.h"

using namespace GameAPI;

#define DEBUG_VERBOSE 0

#if DEBUG_VERBOSE
#define log_debug(module, format_str, ...) logger::push( \
    LOG_FORMAT("M", module, format_str, ## __VA_ARGS__), logger::Style::GREY)
#else
#define log_debug(module, format_str, ...)
#endif

// globals
uint8_t KFCA_VOL_SOUND = 96;
uint8_t KFCA_VOL_HEADPHONE = 96;
uint8_t KFCA_VOL_EXTERNAL = 96;
uint8_t KFCA_VOL_WOOFER = 96;

// static stuff
static uint8_t STATUS_BUFFER[64] {};
static bool STATUS_BUFFER_FREEZE = false;
static unsigned int KFCA_VOLL = 0;
static unsigned int KFCA_VOLR = 0;

/*
 * Implementations
 */

static int __cdecl ac_io_kfca_control_button_led(unsigned int button, bool state) {

    // Sound Voltex
    if (avs::game::is_model("KFC")) {

        // control mapping
        static const size_t mapping[] = {
                games::sdvx::Lights::BT_A,
                games::sdvx::Lights::BT_B,
                games::sdvx::Lights::BT_C,
                games::sdvx::Lights::BT_D,
                games::sdvx::Lights::FX_L,
                games::sdvx::Lights::FX_R,
                games::sdvx::Lights::START,
                games::sdvx::Lights::GENERATOR_B,
        };

        // check if button is mapped
        if (button < 8) {

            // get lights
            auto &lights = games::sdvx::get_lights();

            // write light
            float value = state ? 1.f : 0.f;
            Lights::writeLight(RI_MGR, lights.at(mapping[button]), value);
        }
    }

    // Scotto
    if (avs::game::is_model("NSC")) {

        // control mapping
        static const size_t mapping[] = {
                games::scotto::Lights::PAD_F_B,
                games::scotto::Lights::PAD_E_R,
                games::scotto::Lights::PAD_E_B,
                ~0u,
                ~0u,
                ~0u,
                games::scotto::Lights::PAD_F_R,
                games::scotto::Lights::BUTTON,
        };

        // check if button is mapped
        if (button < std::size(mapping) && button[mapping] != ~0u) {

            // get lights
            auto &lights = games::scotto::get_lights();

            // write light
            float value = state ? 1.f : 0.f;
            Lights::writeLight(RI_MGR, lights.at(mapping[button]), value);
        }
    }

    // return success
    return 1;
}

static int __cdecl ac_io_kfca_control_coin_blocker_close(int a1) {
    eamuse_coin_set_block(true);
    return 1;
}

static int __cdecl ac_io_kfca_control_coin_blocker_open(int a1) {
    eamuse_coin_set_block(false);
    return 1;
}

static int __cdecl ac_io_kfca_control_led_bright(uint32_t led_field, uint8_t brightness) {

    // Sound Voltex
    if (avs::game::is_model("KFC")) {

        // get lights
        auto &lights = games::sdvx::get_lights();

        // control mapping
        static const size_t mapping[] {
                games::sdvx::Lights::WING_LEFT_UP_R,
                games::sdvx::Lights::WING_LEFT_UP_G,
                games::sdvx::Lights::WING_LEFT_UP_B,
                games::sdvx::Lights::WING_RIGHT_UP_R,
                games::sdvx::Lights::WING_RIGHT_UP_G,
                games::sdvx::Lights::WING_RIGHT_UP_B,
                games::sdvx::Lights::WING_LEFT_LOW_R,
                games::sdvx::Lights::WING_LEFT_LOW_G,
                games::sdvx::Lights::WING_LEFT_LOW_B,
                games::sdvx::Lights::WING_RIGHT_LOW_R,
                games::sdvx::Lights::WING_RIGHT_LOW_G,
                games::sdvx::Lights::WING_RIGHT_LOW_B,
                games::sdvx::Lights::WOOFER_R,
                games::sdvx::Lights::WOOFER_G,
                games::sdvx::Lights::WOOFER_B,
                games::sdvx::Lights::CONTROLLER_R,
                games::sdvx::Lights::CONTROLLER_G,
                games::sdvx::Lights::CONTROLLER_B,
                games::sdvx::Lights::GENERATOR_R,
                games::sdvx::Lights::GENERATOR_G,
        };

        // write light
        float value = brightness / 255.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
            }
        }
    }

    // BeatStream
    if (avs::game::is_model("NBT")) {

        // get lights
        auto &lights = games::bs::get_lights();

        // mapping
        static const size_t mapping[] {
                ~0u, ~0u, ~0u,
                games::bs::Lights::RightR,
                games::bs::Lights::RightG,
                games::bs::Lights::RightB,
                games::bs::Lights::LeftR,
                games::bs::Lights::LeftG,
                games::bs::Lights::LeftB,
                games::bs::Lights::BottomR,
                games::bs::Lights::BottomG,
                games::bs::Lights::BottomB,
        };

        // write light
        float value = brightness / 127.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (mapping[i] != ~0u && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
            }
        }
    }

    // Nostalgia
    if (avs::game::is_model("PAN")) {

        // get lights
        auto &lights = games::nost::get_lights();

        // mapping
        static const size_t mapping[] {
                ~0u, ~0u, ~0u,
                games::nost::Lights::TitleR,
                games::nost::Lights::TitleG,
                games::nost::Lights::TitleB,
                ~0u, ~0u, ~0u,
                games::nost::Lights::BottomR,
                games::nost::Lights::BottomG,
                games::nost::Lights::BottomB,
        };

        // write light
        float value = brightness / 127.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (mapping[i] != ~0u && led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
            }
        }
    }

    // Scotto
    if (avs::game::is_model("NSC")) {

        // get lights
        auto &lights = games::scotto::get_lights();

        // mapping
        static const size_t mapping[] {
                games::scotto::Lights::CUP_R,
                games::scotto::Lights::CUP_G,
                games::scotto::Lights::CUP_B,
                games::scotto::Lights::PAD_A_R,
                games::scotto::Lights::PAD_A_G,
                games::scotto::Lights::PAD_A_B,
                games::scotto::Lights::PAD_B_R,
                games::scotto::Lights::PAD_B_G,
                games::scotto::Lights::PAD_B_B,
                games::scotto::Lights::PAD_C_R,
                games::scotto::Lights::PAD_C_G,
                games::scotto::Lights::PAD_C_B,
                games::scotto::Lights::PAD_D_R,
                games::scotto::Lights::PAD_D_G,
                games::scotto::Lights::PAD_D_B,
                games::scotto::Lights::FIRST_PAD_R,
                games::scotto::Lights::FIRST_PAD_G,
                games::scotto::Lights::FIRST_PAD_B,
                games::scotto::Lights::PAD_F_G,
                games::scotto::Lights::PAD_E_G,
        };

        // write light
        float value = brightness / 255.f;
        for (size_t i = 0; i < std::size(mapping); i++) {
            if (led_field & (1 << i)) {
                Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
            }
        }
    }

    // return success
    return 1;
}

static char __cdecl ac_io_kfca_current_coinstock(int a1, DWORD *a2) {
    *a2 = (DWORD) eamuse_coin_get_stock();
    return 1;
}

static void *__cdecl ac_io_kfca_get_control_status_buffer(void *target_buffer) {

    // copy buffer
    return memcpy(target_buffer, STATUS_BUFFER, 64);
}

static int __cdecl ac_io_kfca_lock_coincounter(int a1) {
    eamuse_coin_set_block(true);
    return 1;
}

static bool __cdecl ac_io_kfca_req_volume_control(
        uint8_t vol_sound, uint8_t vol_headphone, uint8_t vol_external, uint8_t vol_woofer) {

    // update globals
    KFCA_VOL_SOUND = vol_sound;
    KFCA_VOL_HEADPHONE = vol_headphone;
    KFCA_VOL_EXTERNAL = vol_external;
    KFCA_VOL_WOOFER = vol_woofer;

    // Sound Voltex
    if (avs::game::is_model("KFC")) {

        // get lights
        auto &lights = games::sdvx::get_lights();
        GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_SOUND],
                                    (100 - vol_sound) / 100.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_HEADPHONE],
                                    (100 - vol_headphone) / 100.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_EXTERNAL],
                                    (100 - vol_external) / 100.f);
        GameAPI::Lights::writeLight(RI_MGR, lights[games::sdvx::Lights::VOLUME_WOOFER],
                                    (100 - vol_woofer) / 100.f);
    }

    return true;
}

static bool __cdecl ac_io_kfca_set_watchdog_time(short a1) {
    return true;
}

static char __cdecl ac_io_kfca_unlock_coincounter(int a1) {
    eamuse_coin_set_block(false);
    return 1;
}

static char __cdecl ac_io_kfca_update_control_status_buffer() {
    static const int input_offset = 4;

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    memset(STATUS_BUFFER, 0, 64);

    // SDVX
    if (avs::game::is_model("KFC")) {

        // get buttons
        auto &buttons = games::sdvx::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Test))) {
            STATUS_BUFFER[input_offset + 1] |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Service))) {
            STATUS_BUFFER[input_offset + 1] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::CoinMech))) {
            STATUS_BUFFER[input_offset + 1] |= 0x04;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Start))) {
            STATUS_BUFFER[input_offset + 9] |= 0x08;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_A))) {
            STATUS_BUFFER[input_offset + 9] |= 0x04;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_B))) {
            STATUS_BUFFER[input_offset + 9] |= 0x02;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_C))) {
            STATUS_BUFFER[input_offset + 9] |= 0x01;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::BT_D))) {
            STATUS_BUFFER[input_offset + 11] |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::FX_L))) {
            STATUS_BUFFER[input_offset + 11] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::FX_R))) {
            STATUS_BUFFER[input_offset + 11] |= 0x08;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::Headphone))) {
            STATUS_BUFFER[input_offset + 9] |= 0x20;
        }

        // volume left
        const auto now = get_performance_milliseconds();
        const auto vol_l_state = socd::socd_clean(0,
            Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_L_Left)),
            Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_L_Right)),
            now);
        if (vol_l_state == socd::SocdCCW) {
            KFCA_VOLL = (KFCA_VOLL - games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        } else if (vol_l_state == socd::SocdCW) {
            KFCA_VOLL = (KFCA_VOLL + games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        }

        // volume right
        const auto vol_r_state = socd::socd_clean(1,
            Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_R_Left)),
            Buttons::getState(RI_MGR, buttons.at(games::sdvx::Buttons::VOL_R_Right)),
            now);
        if (vol_r_state == socd::SocdCCW) {
            KFCA_VOLR = (KFCA_VOLR - games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        } else if (vol_r_state == socd::SocdCW) {
            KFCA_VOLR = (KFCA_VOLR + games::sdvx::DIGITAL_KNOB_SENS) & 1023;
        }

        // update volumes
        auto &analogs = games::sdvx::get_analogs();
        auto vol_left = KFCA_VOLL;
        auto vol_right = KFCA_VOLR;
        if (analogs.at(0).isSet() || analogs.at(1).isSet()) {
            vol_left += (unsigned int) (Analogs::getState(RI_MGR,
                                                          analogs.at(games::sdvx::Analogs::VOL_L)) * 1023.99f);
            vol_right += (unsigned int) (Analogs::getState(RI_MGR,
                                                           analogs.at(games::sdvx::Analogs::VOL_R)) * 1023.99f);
        }

        // proper loops
        vol_left %= 1024;
        vol_right %= 1024;
        
        log_debug("kfca", "knobs = {} {}", vol_left, vol_right);

        // save volumes in buffer
        STATUS_BUFFER[input_offset + 16 + 0] |= (unsigned char) ((vol_left << 6) & 0xFF);
        STATUS_BUFFER[input_offset + 16 + 1] |= (unsigned char) ((vol_left >> 2) & 0xFF);
        STATUS_BUFFER[input_offset + 16 + 2] |= (unsigned char) ((vol_right << 6) & 0xFF);
        STATUS_BUFFER[input_offset + 16 + 3] |= (unsigned char) ((vol_right >> 2) & 0xFF);
    }

    // Beatstream
    if (avs::game::is_model("NBT")) {

        // get buttons
        auto &buttons = games::bs::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::bs::Buttons::Test))) {
            STATUS_BUFFER[input_offset + 1] |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bs::Buttons::Service))) {
            STATUS_BUFFER[input_offset + 1] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::bs::Buttons::CoinMech))) {
            STATUS_BUFFER[input_offset + 1] |= 0x04;
        }
    }

    // Nostalgia
    if (avs::game::is_model("PAN")) {

        // get buttons
        auto &buttons = games::nost::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::nost::Buttons::Service))) {
            STATUS_BUFFER[input_offset + 1] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::nost::Buttons::Test))) {
            STATUS_BUFFER[input_offset + 1] |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::nost::Buttons::CoinMech))) {
            STATUS_BUFFER[input_offset + 1] |= 0x04;
        }
    }

    // Scotto
    if (avs::game::is_model("NSC")) {

        // get buttons
        auto &buttons = games::scotto::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Test)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[input_offset + 1] |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Service)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[input_offset + 1] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::CoinMech)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[input_offset + 1] |= 0x04;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Start)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[input_offset + 9] |= 0x20;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Up)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[input_offset + 9] |= 0x10;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Down)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[input_offset + 9] |= 0x08;
        }

        // the code also checks `input_offset + 9` for 0x01 but that does not trigger any response
        // in the "I/O CHECK" scene
    }

    // success
    return true;
}

static void __cdecl ac_io_kfca_watchdog_off() {
}

// yes this is spelled "marge" instead of "merge"
static int __cdecl ac_io_kfca_set_status_marge_func(void *cb) {
    return 1;
}

/*
 * Module stuff
 */

acio::KFCAModule::KFCAModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("KFCA", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::KFCAModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_kfca_control_button_led);
    ACIO_MODULE_HOOK(ac_io_kfca_control_coin_blocker_close);
    ACIO_MODULE_HOOK(ac_io_kfca_control_coin_blocker_open);
    ACIO_MODULE_HOOK(ac_io_kfca_control_led_bright);
    ACIO_MODULE_HOOK(ac_io_kfca_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_kfca_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_kfca_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_kfca_req_volume_control);
    ACIO_MODULE_HOOK(ac_io_kfca_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_kfca_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_kfca_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_kfca_watchdog_off);
    ACIO_MODULE_HOOK(ac_io_kfca_set_status_marge_func);
}
