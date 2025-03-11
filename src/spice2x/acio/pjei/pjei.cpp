#include "pjei.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"
#include "misc/eamuse.h"
#include "games/we/io.h"
#include "avs/game.h"

//using namespace GameAPI;

// static stuff
static uint8_t STATUS_BUFFER[40];
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static bool __cdecl ac_io_pjei_current_coinstock(int a1, uint32_t *coinstock) {
    *coinstock = eamuse_coin_get_stock();
    return true;
}

static bool __cdecl ac_io_pjei_consume_coinstock(int a1, uint32_t amount) {
    return eamuse_coin_consume(amount);
}

static bool __cdecl ac_io_pjei_get_softwareid(char *dst) {
    static char DATA[] = "0140FFFFFFFFFFFFFFFF";
    memcpy(dst, DATA, sizeof(DATA));
    return true;
}

static bool __cdecl ac_io_pjei_get_systemid(char *dst) {
    static char DATA[] = "0140FFFFFFFFFFFFFFFF";
    memcpy(dst, DATA, sizeof(DATA));
    return true;
}

static bool __cdecl ac_io_pjei_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    memset(STATUS_BUFFER, 0, sizeof(STATUS_BUFFER));

    // Winning Eleven
    if (avs::game::is_model({ "KCK", "NCK" })) {

        // get buttons
        auto &buttons = games::we::get_buttons();

        // apply buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Service])) {
            STATUS_BUFFER[16] |= 0x10;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Test])) {
            STATUS_BUFFER[16] |= 0x20;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::CoinMech])) {
            STATUS_BUFFER[16] |= 0x04;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Start])) {
            STATUS_BUFFER[4] |= 0x80;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Up])) {
            STATUS_BUFFER[4] |= 0x40;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Down])) {
            STATUS_BUFFER[4] |= 0x20;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Left])) {
            STATUS_BUFFER[4] |= 0x10;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::Right])) {
            STATUS_BUFFER[4] |= 0x08;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::ButtonA])) {
            STATUS_BUFFER[4] |= 0x04;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::ButtonB])) {
            STATUS_BUFFER[4] |= 0x02;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::ButtonC])) {
            STATUS_BUFFER[4] |= 0x01;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::ButtonD])) {
            STATUS_BUFFER[6] |= 0x80;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::ButtonE])) {
            STATUS_BUFFER[6] |= 0x40;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::ButtonF])) {
            STATUS_BUFFER[6] |= 0x20;
        }
    }

    // success
    return true;
}

static bool ac_io_pjei_get_control_status_buffer(uint8_t *buffer) {
    memcpy(buffer, STATUS_BUFFER, sizeof(STATUS_BUFFER));
    return true;
}

static bool __cdecl ac_io_pjei_req_secplug_check() {
    return true;
}

static bool __cdecl ac_io_pjei_req_secplug_check_isfinished() {
    return true;
}

static bool __cdecl ac_io_pjei_req_secplug_missing_check() {
    return true;
}

static bool __cdecl ac_io_pjei_req_secplug_missing_check_isfinished() {
    return true;
}

static bool __cdecl ac_io_pjei_lock_coincounter(int a1) {
    eamuse_coin_set_block(true);
    return true;
}

static bool __cdecl ac_io_pjei_unlock_coincounter(int a1) {
    eamuse_coin_set_block(false);
    return true;
}

static bool __cdecl ac_io_pjei_control_coin_blocker_on(bool a1) {
    eamuse_coin_set_block(true);
    return true;
}

static bool __cdecl ac_io_pjei_control_coin_blocker_off(bool a1) {
    eamuse_coin_set_block(false);
    return true;
}

/*
 * Helper method for easily setting the light values
 */
static void ac_io_pjei_control_lamp_set(uint8_t lamp_bits, uint8_t brightness) {
    auto &lights = games::we::get_lights();
    float value = CLAMP(brightness / 31.f, 0.f, 1.f);
    if (lamp_bits & 0x20) {
        GameAPI::Lights::writeLight(RI_MGR, lights[games::we::Lights::LeftRed], value);
    }
    if (lamp_bits & 0x10) {
        GameAPI::Lights::writeLight(RI_MGR, lights[games::we::Lights::LeftGreen], value);
    }
    if (lamp_bits & 0x08) {
        GameAPI::Lights::writeLight(RI_MGR, lights[games::we::Lights::LeftBlue], value);
    }
    if (lamp_bits & 0x04) {
        GameAPI::Lights::writeLight(RI_MGR, lights[games::we::Lights::RightRed], value);
    }
    if (lamp_bits & 0x02) {
        GameAPI::Lights::writeLight(RI_MGR, lights[games::we::Lights::RightGreen], value);
    }
    if (lamp_bits & 0x01) {
        GameAPI::Lights::writeLight(RI_MGR, lights[games::we::Lights::RightBlue], value);
    }
}

static bool __cdecl ac_io_pjei_control_lamp_on(uint8_t lamp_bits) {
    ac_io_pjei_control_lamp_set(lamp_bits, 31);
    return true;
}

static bool __cdecl ac_io_pjei_control_lamp_off(uint8_t lamp_bits) {
    ac_io_pjei_control_lamp_set(lamp_bits, 0);
    return true;
}

static bool __cdecl ac_io_pjei_control_lamp_bright(uint8_t lamp_bit, uint8_t brightness) {
    ac_io_pjei_control_lamp_set(lamp_bit, brightness);
    return true;
}

static bool __cdecl ac_io_pjei_control_lamp_mode(int mode) {
    // mode -> [0,1] (0 is static, 1 is brightness?)
    return true;
}

/*
 * Module stuff
 */
acio::PJEIModule::PJEIModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("PJEI", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::PJEIModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_pjei_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_pjei_consume_coinstock);
    ACIO_MODULE_HOOK(ac_io_pjei_get_softwareid);
    ACIO_MODULE_HOOK(ac_io_pjei_get_systemid);
    ACIO_MODULE_HOOK(ac_io_pjei_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_pjei_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_pjei_req_secplug_check);
    ACIO_MODULE_HOOK(ac_io_pjei_req_secplug_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_pjei_req_secplug_missing_check);
    ACIO_MODULE_HOOK(ac_io_pjei_req_secplug_missing_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_pjei_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_pjei_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_pjei_control_coin_blocker_on);
    ACIO_MODULE_HOOK(ac_io_pjei_control_coin_blocker_off);
    ACIO_MODULE_HOOK(ac_io_pjei_control_lamp_on);
    ACIO_MODULE_HOOK(ac_io_pjei_control_lamp_off);
    ACIO_MODULE_HOOK(ac_io_pjei_control_lamp_bright);
    ACIO_MODULE_HOOK(ac_io_pjei_control_lamp_mode);
}
