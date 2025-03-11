#include "hbhi.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "misc/eamuse.h"
#include "games/rf3d/io.h"
#include "games/sc/io.h"
#include "games/hpm/io.h"
#include "avs/game.h"
#include "util/logging.h"
#include "util/utils.h"

using namespace GameAPI;

// state
static uint8_t STATUS_BUFFER[64] {};
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static int __cdecl ac_io_hbhi_add_coin(int a1, int a2) {
    eamuse_coin_add();
    return 1;
}

static char __cdecl ac_io_hbhi_consume_coinstock(int a1, int a2) {
    eamuse_coin_consume_stock();
    return 1;
}

static int __cdecl ac_io_hbhi_control_coin_blocker_close(int a1) {
    eamuse_coin_set_block(true);
    return 1;
}

static int __cdecl ac_io_hbhi_control_coin_blocker_open(int a1) {
    eamuse_coin_set_block(0);
    return 1;
}

/*
 * Helper method, not a real ACIO one
 */
static inline int __cdecl ac_io_hbhi_control_lamp_set(uint32_t lamp_bits, float value) {

    // steel chronicle
    if (avs::game::is_model("KGG")) {

        // get lights
        auto &lights = games::sc::get_lights();

        // write lights
        if (lamp_bits & 0x01) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::SideRed), value);
        }
        if (lamp_bits & 0x02) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::SideGreen), value);
        }
        if (lamp_bits & 0x04) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::SideBlue), value);
        }
        if (lamp_bits & 0x08) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::CenterRed), value);
        }
        if (lamp_bits & 0x10) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::CenterGreen), value);
        }
        if (lamp_bits & 0x20) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::CenterBlue), value);
        }
        if (lamp_bits & 0x40) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::ControllerRed), value);
        }
        if (lamp_bits & 0x80) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::sc::Lights::ControllerBlue), value);
        }
    }

    // hello popn music
    if (avs::game::is_model("JMP")) {

        // get lights
        auto &lights = games::hpm::get_lights();

        // write lights
        if (lamp_bits & 0x01) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P1_RED_P2_GREEN), value);
        }
        if (lamp_bits & 0x02) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P1_BLUE), value);
        }
        if (lamp_bits & 0x04) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P1_YELLOW), value);
        }
        if (lamp_bits & 0x08) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P1_GREEN), value);
        }
        if (lamp_bits & 0x10) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P2_RED), value);
        }
        if (lamp_bits & 0x20) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P2_BLUE), value);
        }
        if (lamp_bits & 0x40) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P2_YELLOW), value);
        }
        if (lamp_bits & 0x80) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P2_START), value);
        }
    }

    // return success
    return 1;
}

static bool __cdecl ac_io_hbhi_control_lamp_bright(uint32_t lamp_bits, uint8_t value) {
    ac_io_hbhi_control_lamp_set(lamp_bits, value / 31.f);
    return true;
}

static int __cdecl ac_io_hbhi_control_lamp_mode(uint32_t mode) {
    return 1;
}

static int __cdecl ac_io_hbhi_control_lamp_off(uint8_t lamp_bits) {
    return ac_io_hbhi_control_lamp_set(lamp_bits, 0.f);
}

static int __cdecl ac_io_hbhi_control_lamp_on(uint8_t lamp_bits) {
    return ac_io_hbhi_control_lamp_set(lamp_bits, 1.f);
}

/*
 * Helper method, not a real ACIO one
 */
static inline int __cdecl ac_io_hbhi_control_parallel_set(uint8_t lamp_bits, float value) {

    // hello popn music
    if (avs::game::is_model("JMP")) {

        // get lights
        auto &lights = games::hpm::get_lights();

        // write lights
        if (lamp_bits & 0x01) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::P1_START), value);
        }
        if (lamp_bits & 0x02) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::SPEAKER_BLUE), value);
        }
        if (lamp_bits & 0x04) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::SPEAKER_ORANGE), value);
        }
        if (lamp_bits & 0x08) {
            GameAPI::Lights::writeLight(RI_MGR, lights.at(games::hpm::Lights::SPEAKER_RED), value);
        }
    }

    // return success
    return 1;
}

static int __cdecl ac_io_hbhi_control_parallel_off(uint8_t lamp_bits) {
    return ac_io_hbhi_control_parallel_set(lamp_bits, 0.f);
}

static int __cdecl ac_io_hbhi_control_parallel_on(uint8_t lamp_bits) {
    return ac_io_hbhi_control_parallel_set(lamp_bits, 1.f);
}

static int __cdecl ac_io_hbhi_control_reset() {
    return 1;
}

static bool __cdecl ac_io_hbhi_create_get_status_thread(void *a1) {
    return true;
}

static char __cdecl ac_io_hbhi_current_coinstock(int a1, int *coinstock) {
    *coinstock = eamuse_coin_get_stock();
    return 1;
}

static int __cdecl ac_io_hbhi_destroy_get_status_thread() {
    return 1;
}

static char __cdecl ac_io_hbhi_get_coin_input_wave_buffer(int *a1) {
    return 1;
}

static void *__cdecl ac_io_hbhi_get_control_status_buffer(uint8_t *buffer) {

    // return buffer
    memcpy(buffer, STATUS_BUFFER, std::size(STATUS_BUFFER));
    return buffer;
}

static char __cdecl ac_io_hbhi_get_softwareid(char *a1) {
    memset(a1, 'F', 16);
    return 1;
}

static char __cdecl ac_io_hbhi_get_systemid(char *a1) {
    memset(a1, 'F', 16);
    return 1;
}

static bool __cdecl ac_io_hbhi_get_watchdog_status() {
    return true;
}

static short __cdecl ac_io_hbhi_get_watchdog_time_min() {
    return 0;
}

static short __cdecl ac_io_hbhi_get_watchdog_time_now() {
    return 0;
}

static char __cdecl ac_io_hbhi_lock_coincounter(int a1) {
    return 1;
}

static char __cdecl ac_io_hbhi_req_carddispenser_disburse() {
    return 1;
}

static bool __cdecl ac_io_hbhi_req_carddispenser_disburse_isfinished(int *a1) {
    *a1 += 1;
    return true;
}

static char __cdecl ac_io_hbhi_req_carddispenser_get_status() {
    return 1;
}

static int __cdecl ac_io_hbhi_req_carddispenser_get_status_isfinished(int *a1) {
    *a1 += 1;
    return 2;
}

static char __cdecl ac_io_hbhi_req_carddispenser_init() {
    return 1;
}

static bool __cdecl ac_io_hbhi_req_carddispenser_init_isfinished(int *a1) {
    *a1 += 1;
    return true;
}

static char __cdecl ac_io_hbhi_req_coin_input_wave() {
    return 1;
}

static char __cdecl ac_io_hbhi_req_get_control_status(int *a1) {
    return 1;
}

static char __cdecl ac_io_hbhi_req_secplug_check(char *a1) {
    return 1;
}

static bool __cdecl ac_io_hbhi_req_secplug_check_isfinished(int *a1) {
    return true;
}

static char __cdecl ac_io_hbhi_req_secplug_check_softwareplug(char *a1) {
    return 1;
}

static char __cdecl ac_io_hbhi_req_secplug_check_systemplug() {
    return 1;
}

static char __cdecl ac_io_hbhi_req_secplug_missing_check() {
    return 1;
}

static bool __cdecl ac_io_hbhi_req_secplug_missing_check_isfinished(int *a1) {
    return true;
}

static bool __cdecl ac_io_hbhi_req_volume_control(char a1, char a2) {
    return true;
}

static bool __cdecl ac_io_hbhi_req_volume_control_isfinished(int *a1) {
    return true;
}

static int __cdecl ac_io_hbhi_reset_coin_slot_noise_flag(int a1) {
    return 1;
}

static int __cdecl ac_io_hbhi_set_framing_err_packet_send_interval(int a1) {
    return 1;
}

static bool __cdecl ac_io_hbhi_set_watchdog_time(short a1) {
    return true;
}

static char __cdecl ac_io_hbhi_unlock_coincounter(int a1) {
    return 1;
}

static char __cdecl ac_io_hbhi_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // steel chronicle
    if (avs::game::is_model("KGG")) {

        // get buttons
        auto &buttons = games::sc::get_buttons();

        // reset
        memset(STATUS_BUFFER, 0, 64);

        // check buttons
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::Service))) {
            STATUS_BUFFER[5] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::Test))) {
            STATUS_BUFFER[5] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::LButton))) {
            STATUS_BUFFER[12] |= 1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::L1))) {
            STATUS_BUFFER[12] |= 1 << 1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::L2))) {
            STATUS_BUFFER[12] |= 1 << 2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::JogLeft))) {
            STATUS_BUFFER[12] |= 1 << 3;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::JogRight))) {
            STATUS_BUFFER[12] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::RButton))) {
            STATUS_BUFFER[12] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::R1))) {
            STATUS_BUFFER[12] |= 1 << 6;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::sc::Buttons::R2))) {
            STATUS_BUFFER[12] |= 1 << 7;
        }

        // get analogs
        auto &analogs = games::sc::get_analogs();

        auto joy_left_x = Analogs::getState(RI_MGR, analogs.at(games::sc::Analogs::LEFT_X)) * USHRT_MAX;
        auto joy_left_y = Analogs::getState(RI_MGR, analogs.at(games::sc::Analogs::LEFT_Y)) * USHRT_MAX;
        auto joy_right_x = Analogs::getState(RI_MGR, analogs.at(games::sc::Analogs::RIGHT_X)) * USHRT_MAX;
        auto joy_right_y = Analogs::getState(RI_MGR, analogs.at(games::sc::Analogs::RIGHT_Y)) * USHRT_MAX;

        // because these are flight sticks, the X axis is inverted
        *((uint16_t *) &STATUS_BUFFER[20]) = USHRT_MAX - (uint16_t) joy_left_x;
        *((uint16_t *) &STATUS_BUFFER[22]) = (uint16_t) joy_left_y;
        *((uint16_t *) &STATUS_BUFFER[24]) = USHRT_MAX - (uint16_t) joy_right_x;
        *((uint16_t *) &STATUS_BUFFER[26]) = (uint16_t) joy_right_y;
    }

    // hello popn music
    if (avs::game::is_model("JMP")) {

        // get buttons
        auto &buttons = games::hpm::get_buttons();

        // reset
        memset(STATUS_BUFFER, 0x00, 64);

        // check buttons
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::Service))) {
            STATUS_BUFFER[5] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::Test))) {
            STATUS_BUFFER[5] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::CoinMech))) {
            STATUS_BUFFER[5] |= 1 << 2;
        }
        if (!Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P1_Start))) {
            STATUS_BUFFER[4] |= 1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P1_1))) {
            STATUS_BUFFER[12] |= 1 << 0;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P1_2))) {
            STATUS_BUFFER[12] |= 1 << 1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P1_3))) {
            STATUS_BUFFER[12] |= 1 << 2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P1_4))) {
            STATUS_BUFFER[12] |= 1 << 3;
        }
        if (!Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P2_Start))) {
            STATUS_BUFFER[6] |= 1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P2_1))) {
            STATUS_BUFFER[12] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P2_2))) {
            STATUS_BUFFER[12] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P2_3))) {
            STATUS_BUFFER[12] |= 1 << 6;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::hpm::Buttons::P2_4))) {
            STATUS_BUFFER[12] |= 1 << 7;
        }
    }

    // road fighters 3D
    if (avs::game::is_model("JGT")) {
        static int lever_state = 0;

        // get buttons
        auto &buttons = games::rf3d::get_buttons();

        // reset
        memset(STATUS_BUFFER, 0x00, 64);

        // check buttons
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::Service))) {
            STATUS_BUFFER[5] |= 1 << 4;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::Test))) {
            STATUS_BUFFER[5] |= 1 << 5;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::CoinMech))) {
            STATUS_BUFFER[5] |= 1 << 2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::View))) {
            STATUS_BUFFER[12] |= 1 << 2;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::Toggle2D3D))) {
            STATUS_BUFFER[12] |= 1 << 3;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::LeverUp))) {
            STATUS_BUFFER[12] |= 1 << 4;
            lever_state = 0;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::LeverDown))) {
            STATUS_BUFFER[12] |= 1 << 5;
            lever_state = 0;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::LeverLeft))) {
            STATUS_BUFFER[12] |= 1 << 6;
            lever_state = 0;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::LeverRight))) {
            STATUS_BUFFER[12] |= 1 << 7;
            lever_state = 0;
        }

        // auto lever buttons
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::AutoLeverUp)) && lever_state < 6) {
            lever_state++;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::AutoLeverDown)) && lever_state > 0) {
            lever_state--;
        }

        // auto lever logic
        switch (lever_state) {
            case 1:
                STATUS_BUFFER[12] |= 1 << 4 | 1 << 6;
                break;
            case 2:
                STATUS_BUFFER[12] |= 1 << 4 | 1 << 6;
                break;
            case 3:
                STATUS_BUFFER[12] |= 1 << 4;
                break;
            case 4:
                STATUS_BUFFER[12] |= 1 << 5;
                break;
            case 5:
                STATUS_BUFFER[12] |= 1 << 4 | 1 << 7;
                break;
            case 6:
                STATUS_BUFFER[12] |= 1 << 5 | 1 << 7;
                break;
            default:
                lever_state = 0;
                break;
        }
    }

    // success
    return true;
}

static void __cdecl ac_io_hbhi_watchdog_off() {
}

/*
 * Module stuff
 */

acio::HBHIModule::HBHIModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("HBHI", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::HBHIModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_hbhi_add_coin);
    ACIO_MODULE_HOOK(ac_io_hbhi_consume_coinstock);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_coin_blocker_close);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_coin_blocker_open);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_lamp_bright);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_lamp_mode);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_lamp_off);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_lamp_on);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_parallel_off);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_parallel_on);
    ACIO_MODULE_HOOK(ac_io_hbhi_control_reset);
    ACIO_MODULE_HOOK(ac_io_hbhi_create_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_hbhi_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_hbhi_destroy_get_status_thread);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_coin_input_wave_buffer);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_softwareid);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_systemid);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_watchdog_status);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_watchdog_time_min);
    ACIO_MODULE_HOOK(ac_io_hbhi_get_watchdog_time_now);
    ACIO_MODULE_HOOK(ac_io_hbhi_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_carddispenser_disburse);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_carddispenser_disburse_isfinished);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_carddispenser_get_status);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_carddispenser_get_status_isfinished);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_carddispenser_init);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_carddispenser_init_isfinished);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_coin_input_wave);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_get_control_status);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_secplug_check);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_secplug_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_secplug_check_softwareplug);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_secplug_check_systemplug);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_secplug_missing_check);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_secplug_missing_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_volume_control);
    ACIO_MODULE_HOOK(ac_io_hbhi_req_volume_control_isfinished);
    ACIO_MODULE_HOOK(ac_io_hbhi_reset_coin_slot_noise_flag);
    ACIO_MODULE_HOOK(ac_io_hbhi_set_framing_err_packet_send_interval);
    ACIO_MODULE_HOOK(ac_io_hbhi_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_hbhi_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_hbhi_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_hbhi_watchdog_off);
}
