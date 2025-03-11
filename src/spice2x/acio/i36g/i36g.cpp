#include "i36g.h"
#include "launcher/launcher.h"
#include "avs/game.h"
#include "rawinput/rawinput.h"
#include "games/mga/io.h"
#include "misc/eamuse.h"
#include "util/utils.h"

using namespace GameAPI;

// static stuff
static uint8_t STATUS_BUFFER[88 * 2] {};
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static int __cdecl ac_io_i36g_add_coin(int a1, int a2, int a3) {

    // not so sure we want to add coins
    return 1;
}

static char __cdecl ac_io_i36g_consume_coinstock(int a1, int a2, int a3) {
    eamuse_coin_consume_stock();
    return 1;
}

static int __cdecl ac_io_i36g_control_coin_blocker_close(int a1, int a2) {
    eamuse_coin_set_block(true);
    return 1;
}

static int __cdecl ac_io_i36g_control_coin_blocker_open(int a1, int a2) {
    eamuse_coin_set_block(false);
    return 1;
}

static int __cdecl ac_io_i36g_control_lamp_bright(uint32_t device, uint32_t lamp_bits, uint8_t brightness) {

    // calculate value
    float value = (float) brightness / 255.f;

    // get lights
    auto &lights = games::mga::get_lights();

    // cabinet device
    if (device == 21) {
        if (lamp_bits & 1) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::LeftR], value);
        }
        if (lamp_bits & 2) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::LeftG], value);
        }
        if (lamp_bits & 4) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::LeftB], value);
        }
        if (lamp_bits & 8) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::RightR], value);
        }
        if (lamp_bits & 16) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::RightG], value);
        }
        if (lamp_bits & 32) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::RightB], value);
        }
        if (lamp_bits & 512) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::Start], value);
        }
    }

    // gun device
    if (device == 22) {
        if (lamp_bits & 1) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::GunR], value);
        }
        if (lamp_bits & 2) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::GunG], value);
        }
        if (lamp_bits & 4) {
            Lights::writeLight(RI_MGR, lights[games::mga::Lights::GunB], value);
        }
    }

    // success
    return 1;
}

static int __cdecl ac_io_i36g_control_motor_power(int device, uint8_t strength) {

    // gun device
    if (device == 22) {
        float value = (float) strength / 255.f;
        auto &lights = games::mga::get_lights();
        Lights::writeLight(RI_MGR, lights[games::mga::Lights::GunVibration], value);
    }

    // success
    return 1;
}

static char __cdecl ac_io_i36g_current_coinstock(int a1, int a2, int *a3) {

    // get coinstock
    *a3 = eamuse_coin_get_stock();
    return 1;
}

static char __cdecl ac_io_i36g_get_coin_input_wave_buffer(int a1, char *a2) {
    return 1;
}

static void __cdecl ac_io_i36g_get_control_status_buffer(int device, void *buffer) {

    // cabinet buffer
    if (device == 21) {
        memcpy(buffer, &STATUS_BUFFER[0], 88);
    }

    // gun buffer
    if (device == 22) {
        memcpy(buffer, &STATUS_BUFFER[88], 88);
    }
}

static char __cdecl ac_io_i36g_get_softwareid(int a1, int a2) {
    return 1;
}

static char __cdecl ac_io_i36g_get_systemid(int a1, int a2) {
    return 1;
}

static bool __cdecl ac_io_i36g_get_watchdog_status(int a1) {
    return false;
}

static short __cdecl ac_io_i36g_get_watchdog_time_min(int a1) {
    return 0;
}

static short __cdecl ac_io_i36g_get_watchdog_time_now(int a1) {
    return 0;
}

static char __cdecl ac_io_i36g_lock_coincounter(int a1, int a2) {
    return 1;
}

static char __cdecl ac_io_i36g_req_coin_input_wave(int a1) {
    return 1;
}

static char __cdecl ac_io_i36g_req_get_control_status(int a1, int *a2) {
    return 1;
}

static char __cdecl ac_io_i36g_req_secplug_check(int a1, char *a2) {
    return 1;
}

static bool __cdecl ac_io_i36g_req_secplug_check_isfinished(int a1, int *a2) {
    return true;
}

static char __cdecl ac_io_i36g_req_secplug_check_softwareplug(int a1, char *a2) {
    return 1;
}

static char __cdecl ac_io_i36g_req_secplug_check_systemplug(int a1) {
    return 1;
}

static char __cdecl ac_io_i36g_req_secplug_missing_check(int a1) {
    return 1;
}

static bool __cdecl ac_io_i36g_req_secplug_missing_check_isfinished(int a1, int *a2) {
    return true;
}

static bool __cdecl ac_io_i36g_req_volume_control(int a1, char a2, char a3, char a4, char a5) {
    return true;
}

static bool __cdecl ac_io_i36g_req_volume_control_isfinished(int a1, int *ret_state) {
    *ret_state = 3;
    return true;
}

static int __cdecl ac_io_i36g_set_cmdmode(int a1, int a2) {
    return 1;
}

static int __cdecl ac_io_i36g_set_framing_err_packet_send_interval(int a1) {
    return 1;
}

static bool __cdecl ac_io_i36g_set_watchdog_time(int a1, short a2) {
    return true;
}

static char __cdecl ac_io_i36g_unlock_coincounter(int a1, int a2) {
    return 1;
}

static bool __cdecl ac_io_i36g_update_control_status_buffer(int node) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // Metal Gear Arcade
    if (avs::game::is_model("I36")) {

        // get buttons
        auto &buttons = games::mga::get_buttons();

        // cabinet device
        if (node == 21) {

            // clear status buffer
            memset(&STATUS_BUFFER[0], 0, 88);

            // update buttons
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::Service))) {
                ARRAY_SETB(&STATUS_BUFFER[0], 44);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::Test))) {
                ARRAY_SETB(&STATUS_BUFFER[0], 45);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::CoinMech))) {
                ARRAY_SETB(&STATUS_BUFFER[0], 42);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::Start))) {
                ARRAY_SETB(&STATUS_BUFFER[0], 124);
            }
        }

        // gun device
        if (node == 22) {

            // clear status buffer
            memset(&STATUS_BUFFER[88], 0, 88);

            // update buttons
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::TriggerButton))
            || (GetKeyState(VK_LBUTTON) & 0x100) != 0) { // mouse button
                ARRAY_SETB(&STATUS_BUFFER[88], 109);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::FrontTop))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 108);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::FrontBottom))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 106);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::SideLeft))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 107);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::SideRight))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 105);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::SideLever))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 104);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::SwitchButton))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 125);
            }
            if (Buttons::getState(RI_MGR, buttons.at(games::mga::Buttons::Top))) {
                ARRAY_SETB(&STATUS_BUFFER[88], 124);
            }

            // joy stick
            unsigned short joy_x = 0x7FFF;
            unsigned short joy_y = 0x7FFF;
            bool joy_x_pressed = false;
            bool joy_y_pressed = false;
            if (Buttons::getState(RI_MGR, buttons[games::mga::Buttons::JoyForwards])) {
                joy_y -= 0x7FFF;
                joy_y_pressed = true;
            }
            if (Buttons::getState(RI_MGR, buttons[games::mga::Buttons::JoyBackwards])) {
                joy_y += 0x7FFF;
                joy_y_pressed = true;
            }
            if (Buttons::getState(RI_MGR, buttons[games::mga::Buttons::JoyLeft])) {
                joy_x -= 0x7FFF;
                joy_x_pressed = true;
            }
            if (Buttons::getState(RI_MGR, buttons[games::mga::Buttons::JoyRight])) {
                joy_x += 0x7FFF;
                joy_x_pressed = true;
            }

            // joy stick raw input
            auto &analogs = games::mga::get_analogs();
            if (!joy_x_pressed && analogs[games::mga::Analogs::JoyX].isSet()) {
                joy_x = (unsigned short) (Analogs::getState(RI_MGR, analogs[games::mga::Analogs::JoyX]) * 0xFFFF);
            }
            if (!joy_y_pressed && analogs[games::mga::Analogs::JoyY].isSet()) {
                joy_y = (unsigned short) (Analogs::getState(RI_MGR, analogs[games::mga::Analogs::JoyY]) * 0xFFFF);
            }

            // save joy stick
            STATUS_BUFFER[88 + 42] = LOBYTE(joy_y);
            STATUS_BUFFER[88 + 43] = HIBYTE(joy_y);
            STATUS_BUFFER[88 + 44] = LOBYTE(joy_x);
            STATUS_BUFFER[88 + 45] = HIBYTE(joy_x);
        }
    }

    // return success
    return true;
}

static int __cdecl ac_io_i36g_watchdog_off(int a1) {
    return 1;
}

/*
 * Module stuff
 */

acio::I36GModule::I36GModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("I36G", module, hookMode) {
    this->status_buffer = &STATUS_BUFFER[0];
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::I36GModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_i36g_add_coin);
    ACIO_MODULE_HOOK(ac_io_i36g_consume_coinstock);
    ACIO_MODULE_HOOK(ac_io_i36g_control_coin_blocker_close);
    ACIO_MODULE_HOOK(ac_io_i36g_control_coin_blocker_open);
    ACIO_MODULE_HOOK(ac_io_i36g_control_lamp_bright);
    ACIO_MODULE_HOOK(ac_io_i36g_control_motor_power);
    ACIO_MODULE_HOOK(ac_io_i36g_current_coinstock);
    ACIO_MODULE_HOOK(ac_io_i36g_get_coin_input_wave_buffer);
    ACIO_MODULE_HOOK(ac_io_i36g_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_i36g_get_softwareid);
    ACIO_MODULE_HOOK(ac_io_i36g_get_systemid);
    ACIO_MODULE_HOOK(ac_io_i36g_get_watchdog_status);
    ACIO_MODULE_HOOK(ac_io_i36g_get_watchdog_time_min);
    ACIO_MODULE_HOOK(ac_io_i36g_get_watchdog_time_now);
    ACIO_MODULE_HOOK(ac_io_i36g_lock_coincounter);
    ACIO_MODULE_HOOK(ac_io_i36g_req_coin_input_wave);
    ACIO_MODULE_HOOK(ac_io_i36g_req_get_control_status);
    ACIO_MODULE_HOOK(ac_io_i36g_req_secplug_check);
    ACIO_MODULE_HOOK(ac_io_i36g_req_secplug_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_i36g_req_secplug_check_softwareplug);
    ACIO_MODULE_HOOK(ac_io_i36g_req_secplug_check_systemplug);
    ACIO_MODULE_HOOK(ac_io_i36g_req_secplug_missing_check);
    ACIO_MODULE_HOOK(ac_io_i36g_req_secplug_missing_check_isfinished);
    ACIO_MODULE_HOOK(ac_io_i36g_req_volume_control);
    ACIO_MODULE_HOOK(ac_io_i36g_req_volume_control_isfinished);
    ACIO_MODULE_HOOK(ac_io_i36g_set_cmdmode);
    ACIO_MODULE_HOOK(ac_io_i36g_set_framing_err_packet_send_interval);
    ACIO_MODULE_HOOK(ac_io_i36g_set_watchdog_time);
    ACIO_MODULE_HOOK(ac_io_i36g_unlock_coincounter);
    ACIO_MODULE_HOOK(ac_io_i36g_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_i36g_watchdog_off);

    // I36S links
    this->hook((void *) ac_io_i36g_update_control_status_buffer,
               "ac_io_i36s_update_control_status_buffer");
    this->hook((void *) ac_io_i36g_get_control_status_buffer,
               "ac_io_i36s_get_control_status_buffer");
    this->hook((void *) ac_io_i36g_set_cmdmode,
               "ac_io_i36s_set_cmdmode");
}
