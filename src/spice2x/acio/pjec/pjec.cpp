#include "pjec.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"
#include "avs/game.h"
#include "games/we/io.h"

//using namespace GameAPI;

// static stuff
static uint8_t STATUS_BUFFER[72];
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static bool __cdecl ac_io_pjec_get_ps2() {
    return true;
}

static void __cdecl ac_io_pjec_get_control_status_buffer(uint8_t *buffer) {
    memcpy(buffer, STATUS_BUFFER, sizeof(STATUS_BUFFER));
}

static bool __cdecl ac_io_pjec_update_control_status_buffer() {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    memset(STATUS_BUFFER, 0, sizeof(STATUS_BUFFER));

    // Winning Eleven
    if (avs::game::is_model({ "KCK", "NCK" })) {
        auto &buttons = games::we::get_buttons();
        auto &analogs = games::we::get_analogs();

        /*
         * Device Types
         * 0x00 - Unknown Device
         * 0x01 - Mouse
         * 0x02 - Rotate Controller
         * 0x03 - Gun Controller K
         * 0x04 - Digital Controller       <- Accepted
         * 0x05 - Analog Joystick
         * 0x06 - Gun Controller N
         * 0x07 - Analog Controller        <- Accepted
         * 0x08 - USB Analog Controller
         */

        // set device type
        STATUS_BUFFER[0] = 0x07;

        // set device present
        STATUS_BUFFER[2] = 0x5A;

        // reset analogs to center
        STATUS_BUFFER[8] = 0x7F;
        STATUS_BUFFER[9] = 0x7F;
        STATUS_BUFFER[10] = 0x7F;
        STATUS_BUFFER[11] = 0x7F;

        // apply analogs
        if (analogs[games::we::Analogs::PadStickLeftX].isSet()) {
            STATUS_BUFFER[8] = (uint8_t) (GameAPI::Analogs::getState(RI_MGR,
                    analogs[games::we::Analogs::PadStickLeftX]) * 255.9999f);
        }
        if (analogs[games::we::Analogs::PadStickLeftY].isSet()) {
            STATUS_BUFFER[9] = (uint8_t) (GameAPI::Analogs::getState(RI_MGR,
                    analogs[games::we::Analogs::PadStickLeftY]) * 255.9999f);
        }
        if (analogs[games::we::Analogs::PadStickRightX].isSet()) {
            STATUS_BUFFER[10] = (uint8_t) (GameAPI::Analogs::getState(RI_MGR,
                    analogs[games::we::Analogs::PadStickRightX]) * 255.9999f);
        }
        if (analogs[games::we::Analogs::PadStickRightY].isSet()) {
            STATUS_BUFFER[11] = (uint8_t) (GameAPI::Analogs::getState(RI_MGR,
                    analogs[games::we::Analogs::PadStickRightY]) * 255.9999f);
        }

        // apply buttons
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadStart])) {
            STATUS_BUFFER[4] |= 0x08;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadSelect])) {
            STATUS_BUFFER[4] |= 0x01;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadUp])) {
            STATUS_BUFFER[4] |= 0x10;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadDown])) {
            STATUS_BUFFER[4] |= 0x40;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadLeft])) {
            STATUS_BUFFER[4] |= 0x80;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadRight])) {
            STATUS_BUFFER[4] |= 0x20;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadTriangle])) {
            STATUS_BUFFER[5] |= 0x10;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadCross])) {
            STATUS_BUFFER[5] |= 0x40;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadSquare])) {
            STATUS_BUFFER[5] |= 0x80;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadCircle])) {
            STATUS_BUFFER[5] |= 0x20;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadL1])) {
            STATUS_BUFFER[5] |= 0x04;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadL2])) {
            STATUS_BUFFER[5] |= 0x01;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadL3])) {
            STATUS_BUFFER[4] |= 0x02;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadR1])) {
            STATUS_BUFFER[5] |= 0x08;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadR2])) {
            STATUS_BUFFER[5] |= 0x02;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons[games::we::Buttons::PadR3])) {
            STATUS_BUFFER[4] |= 0x04;
        }
    }

    // success
    return true;
}

/*
 * Module stuff
 */

acio::PJECModule::PJECModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("PJEC", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::PJECModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_pjec_get_ps2);
    ACIO_MODULE_HOOK(ac_io_pjec_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_pjec_update_control_status_buffer);
}
