#include "hgth.h"

#include "acio/icca/icca.h"
#include "avs/game.h"
#include "cfg/api.h"
#include "games/rf3d/io.h"
#include "launcher/launcher.h"

using namespace GameAPI;

// state
static uint8_t STATUS_BUFFER[32] {};
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static int __cdecl ac_io_hgth_set_senddata(int a1) {
    return 1;
}

static char __cdecl ac_io_hgth_update_recvdata() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // Road Fighters 3D
    if (avs::game::is_model("JGT")) {

        // keypad mirror fix
        acio::ICCA_FLIP_ROWS = true;

        // variables
        uint16_t wheel = 0x7FFF;
        uint16_t accelerator = 0x00;
        uint16_t brake = 0x00;

        // get buttons
        auto &buttons = games::rf3d::get_buttons();

        // check buttons
        bool wheel_button = false;
        bool accelerate_button = false;
        bool brake_button = false;
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::WheelLeft))) {
            wheel -= 0x7FFF;
            wheel_button = true;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::WheelRight))) {
            wheel += 0x8000;
            wheel_button = true;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::Accelerate))) {
            accelerator = 0xFFFF;
            accelerate_button = true;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::rf3d::Buttons::Brake))) {
            brake = 0xFFFF;
            brake_button = true;
        }

        // analogs
        auto &analogs = games::rf3d::get_analogs();
        if (!wheel_button && analogs.at(games::rf3d::Analogs::Wheel).isSet()) {
            wheel = (uint16_t) (Analogs::getState(RI_MGR, analogs.at(games::rf3d::Analogs::Wheel)) * 0xFFFF);
        }
        if (!accelerate_button && analogs.at(games::rf3d::Analogs::Accelerate).isSet()) {
            accelerator = (uint16_t) (Analogs::getState(RI_MGR, analogs.at(games::rf3d::Analogs::Accelerate)) * 0xFFFF);
        }
        if (!brake_button && analogs.at(games::rf3d::Analogs::Brake).isSet()) {
            brake = (uint16_t) (Analogs::getState(RI_MGR, analogs.at(games::rf3d::Analogs::Brake)) * 0xFFFF);
        }

        // write values
        *((uint16_t *) STATUS_BUFFER + 1) = wheel;
        *((uint16_t *) STATUS_BUFFER + 2) = accelerator;
        *((uint16_t *) STATUS_BUFFER + 3) = brake;
    }

    // success
    return true;
}

static void __cdecl ac_io_hgth_get_recvdata(void *buffer) {

    // copy buffer
    memcpy(buffer, STATUS_BUFFER, sizeof(STATUS_BUFFER));
}

static char __cdecl ac_io_hgth_directreq_set_handle_limit(char a1, int *a2) {
    *a2 = 1;
    return 1;
}

static bool __cdecl ac_io_hgth_directreq_set_handle_limit_isfinished(int *a1) {
    *a1 = 2;
    return true;
}

/*
 * Module stuff
 */

acio::HGTHModule::HGTHModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("HGTH", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::HGTHModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_hgth_set_senddata);
    ACIO_MODULE_HOOK(ac_io_hgth_update_recvdata);
    ACIO_MODULE_HOOK(ac_io_hgth_get_recvdata);
    ACIO_MODULE_HOOK(ac_io_hgth_directreq_set_handle_limit);
    ACIO_MODULE_HOOK(ac_io_hgth_directreq_set_handle_limit_isfinished);
}
