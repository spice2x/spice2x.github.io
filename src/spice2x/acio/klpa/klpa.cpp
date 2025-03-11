#include "klpa.h"

#include "avs/game.h"
#include "games/loveplus/io.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"

using namespace GameAPI;

static uint8_t STATUS_BUFFER[48];
static bool STATUS_BUFFER_FREEZE = false;

static const size_t LOVEPLUS_LIGHTS_MAPPING[] = {
    games::loveplus::Lights::Red,
    games::loveplus::Lights::Green,
    games::loveplus::Lights::Blue,
    SIZE_MAX,
    games::loveplus::Lights::Right,
    games::loveplus::Lights::Left,
};

static char __cdecl ac_io_klpa_consume_coinstock(int a1, DWORD *a2) {
    *a2 = (DWORD) eamuse_coin_get_stock();
    return 1;
}

static int __cdecl ac_io_klpa_control_coin_blocker_close(int a1) {
    eamuse_coin_set_block(true);
    return 1;
}

static int __cdecl ac_io_klpa_control_coin_blocker_open(int a1) {
    eamuse_coin_set_block(false);
    return 1;
}

static int __cdecl ac_io_klpa_control_led_off(size_t index) {

    // LovePlus
    if (avs::game::is_model("KLP") && index < std::size(LOVEPLUS_LIGHTS_MAPPING)) {

        // get lights
        auto &lights = games::loveplus::get_lights();

        if (LOVEPLUS_LIGHTS_MAPPING[index] != SIZE_MAX) {
            Lights::writeLight(RI_MGR, lights.at(LOVEPLUS_LIGHTS_MAPPING[index]), 0.f);
        }
    }

    // return success
    return 1;
}

static int __cdecl ac_io_klpa_control_led_on(size_t index) {

    // LovePlus
    if (avs::game::is_model("KLP") && index < std::size(LOVEPLUS_LIGHTS_MAPPING)) {

        // get lights
        auto &lights = games::loveplus::get_lights();

        if (LOVEPLUS_LIGHTS_MAPPING[index] != SIZE_MAX) {
            Lights::writeLight(RI_MGR, lights.at(LOVEPLUS_LIGHTS_MAPPING[index]), 1.f);
        }
    }

    // return success
    return 1;
}

static bool __cdecl ac_io_klpa_create_get_status_thread() {
    return 1;
}

static char __cdecl ac_io_klpa_current_coinstock(int a1, DWORD *a2) {

    // check bounds
    if (a1 < 0 || a1 >= 2) {
        return 0;
    }

    *a2 = (DWORD) eamuse_coin_get_stock();

    // return success
    return 1;
}

static bool __cdecl ac_io_klpa_destroy_get_status_thread() {
    return 1;
}

static void* __cdecl ac_io_klpa_get_control_status_buffer(void *a1) {

    // copy buffer
    return memcpy(a1, STATUS_BUFFER, sizeof(STATUS_BUFFER));
}

static void __cdecl ac_io_klpa_get_io_command_mode(void *a1) {
    memset(a1, 0, 4);
}

static int __cdecl ac_io_klpa_led_reset() {
    if (avs::game::is_model("KLP")) {

        // get lights
        auto &lights = games::loveplus::get_lights();

        for (const auto &mapping : LOVEPLUS_LIGHTS_MAPPING) {
            if (mapping != SIZE_MAX) {
                Lights::writeLight(RI_MGR, lights.at(mapping), 0.f);
            }
        }
    }

    return 1;
}

static int __cdecl ac_io_klpa_lock_coincounter(int a1) {
    eamuse_coin_set_block(true);
    return 1;
}

static bool __cdecl ac_io_klpa_set_io_command_mode(int a1) {
    return true;
}

static bool __cdecl ac_io_klpa_set_io_command_mode_is_finished(uint8_t *a1) {
    *a1 = 0;
    return true;
}

static int __cdecl ac_io_klpa_set_led_bright(size_t index, uint8_t brightness) {

    // LovePlus
    if (avs::game::is_model("KLP") && index < std::size(LOVEPLUS_LIGHTS_MAPPING)) {

        // get lights
        auto &lights = games::loveplus::get_lights();

        if (LOVEPLUS_LIGHTS_MAPPING[index] != SIZE_MAX) {
            Lights::writeLight(RI_MGR, lights.at(LOVEPLUS_LIGHTS_MAPPING[index]), brightness / 127.f);
        }
    }

    return 1;
}

static bool __cdecl ac_io_klpa_set_sound_mute(int a1) {
    return true;
}

static bool __cdecl ac_io_klpa_set_sound_mute_is_finished(int a1) {
    return true;
}

static bool __cdecl ac_io_klpa_set_watchdog_time(short a1) {
    return true;
}

static char __cdecl ac_io_klpa_unlock_coincounter(int a1) {
    eamuse_coin_set_block(false);
    return 1;
}

static bool __cdecl ac_io_klpa_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // reset buffer
    memset(STATUS_BUFFER, 0, sizeof(STATUS_BUFFER));

    // LovePlus
    if (avs::game::is_model("KLP")) {

        // get buttons
        auto &buttons = games::loveplus::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::loveplus::Buttons::Test)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[5] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::loveplus::Buttons::Service)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[5] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::loveplus::Buttons::Left)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[12] |= 1 << 6;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::loveplus::Buttons::Right)) == Buttons::State::BUTTON_PRESSED) {
            STATUS_BUFFER[12] |= 1 << 7;
        }

        // x[9] & 0x3F) = volume output level?
        // x[11] & 0x3F) = volume output level?
        // x[12] |= (1 << 4) = headphone jack
    }

    // success
    return true;
}

/*
 * Module stuff
 */

acio::KLPAModule::KLPAModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("KLPA", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::KLPAModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_klpa_consume_coinstock);
    ACIO_MODULE_HOOK(ac_io_klpa_control_coin_blocker_close);
    ACIO_MODULE_HOOK(ac_io_klpa_control_coin_blocker_open);
    ACIO_MODULE_HOOK(ac_io_klpa_control_led_off);
    ACIO_MODULE_HOOK(ac_io_klpa_control_led_on);
    ACIO_MODULE_HOOK(ac_io_klpa_create_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_klpa_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_klpa_destroy_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_klpa_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_klpa_get_io_command_mode);
    ACIO_MODULE_HOOK(ac_io_klpa_led_reset);
    ACIO_MODULE_HOOK(ac_io_klpa_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_klpa_set_io_command_mode);
    ACIO_MODULE_HOOK(ac_io_klpa_set_io_command_mode_is_finished);
    ACIO_MODULE_HOOK(ac_io_klpa_set_led_bright);
    ACIO_MODULE_HOOK(ac_io_klpa_set_sound_mute);
    ACIO_MODULE_HOOK(ac_io_klpa_set_sound_mute_is_finished);
    ACIO_MODULE_HOOK(ac_io_klpa_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_klpa_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_klpa_update_control_status_buffer);
}
