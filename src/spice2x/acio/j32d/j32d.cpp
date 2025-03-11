#include "j32d.h"

#include "avs/game.h"
#include "games/ftt/io.h"
#include "games/scotto/io.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"

using namespace GameAPI;

// static stuff
static uint32_t STATUS_BUFFER[20] {};
static bool STATUS_BUFFER_FREEZE = false;
static uint32_t STATUS_BUFFER_COUNTER = 1;

/*
 * Implementations
 */

static bool __cdecl ac_io_j32d_get_control_status_buffer(size_t a1, void* buffer, int a3) {

    // set counter
    STATUS_BUFFER[14] = STATUS_BUFFER_COUNTER++;

    // copy buffer
    memcpy(buffer, STATUS_BUFFER, sizeof(STATUS_BUFFER));

    // return success
    return true;
}


static bool __cdecl ac_io_j32d_update_control_status_buffer(size_t a1) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    memset(STATUS_BUFFER, 0, sizeof(STATUS_BUFFER));

    // FutureTomTom
    if (avs::game::is_model("MMD")) {

        // process buttons
        auto &buttons = games::ftt::get_buttons();
        float pad1_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::ftt::Buttons::Pad1));
        float pad2_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::ftt::Buttons::Pad2));
        float pad3_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::ftt::Buttons::Pad3));
        float pad4_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::ftt::Buttons::Pad4));
        // FIXME(felix): this logic seems wrong for analog handling but correct for digital inputs
        if (pad1_vel > 0.f) {
            STATUS_BUFFER[6] = (int) (51.f * pad1_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ftt::Buttons::Pad2))) {
            STATUS_BUFFER[7] = (int) (51.f * pad2_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ftt::Buttons::Pad3))) {
            STATUS_BUFFER[8] = (int) (51.f * pad3_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::ftt::Buttons::Pad4))) {
            STATUS_BUFFER[9] = (int) (51.f * pad4_vel + 0.5f);
        }

        // process analogs
        auto &analogs = games::ftt::get_analogs();
        auto pad1_analog = analogs.at(games::ftt::Analogs::Pad1);
        auto pad2_analog = analogs.at(games::ftt::Analogs::Pad2);
        auto pad3_analog = analogs.at(games::ftt::Analogs::Pad3);
        auto pad4_analog = analogs.at(games::ftt::Analogs::Pad4);
        if (pad1_analog.isSet()) {
            auto val = (uint32_t) (51.f * Analogs::getState(RI_MGR, pad1_analog) + 0.5f);
            STATUS_BUFFER[6] = MAX(STATUS_BUFFER[6], val);
        }
        if (pad2_analog.isSet()) {
            auto val = (uint32_t) (51.f * Analogs::getState(RI_MGR, pad2_analog) + 0.5f);
            STATUS_BUFFER[7] = MAX(STATUS_BUFFER[7], val);
        }
        if (pad3_analog.isSet()) {
            auto val = (uint32_t) (51.f * Analogs::getState(RI_MGR, pad3_analog) + 0.5f);
            STATUS_BUFFER[8] = MAX(STATUS_BUFFER[8], val);
        }
        if (pad4_analog.isSet()) {
            auto val = (uint32_t) (51.f * Analogs::getState(RI_MGR, pad4_analog) + 0.5f);
            STATUS_BUFFER[9] = MAX(STATUS_BUFFER[9], val);
        }
    }

    // Scotto
    if (avs::game::is_model("NSC")) {

        // get buttons
        auto &buttons = games::scotto::get_buttons();

        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Cup1))) {
            STATUS_BUFFER[5] |= 0x1;
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::Cup2))) {
            STATUS_BUFFER[5] |= 0x2;
        }

        // process button emulation for pads
        float first_pad_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::FirstPad));
        float pad_a_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::PadA));
        float pad_b_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::PadB));
        float pad_c_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::PadC));
        float pad_d_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::PadD));
        float pad_e_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::PadE));
        float pad_f_vel = Buttons::getVelocity(RI_MGR, buttons.at(games::scotto::Buttons::PadF));
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::FirstPad))) {
            STATUS_BUFFER[6] = (int) (191.f * first_pad_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::PadA))) {
            STATUS_BUFFER[7] = (int) (51.f * pad_a_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::PadB))) {
            STATUS_BUFFER[8] = (int) (51.f * pad_b_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::PadC))) {
            STATUS_BUFFER[9] = (int) (51.f * pad_c_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::PadD))) {
            STATUS_BUFFER[10] = (int) (51.f * pad_d_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::PadE))) {
            STATUS_BUFFER[11] = (int) (51.f * pad_e_vel + 0.5f);
        }
        if (Buttons::getState(RI_MGR, buttons.at(games::scotto::Buttons::PadF))) {
            STATUS_BUFFER[12] = (int) (51.f * pad_f_vel + 0.5f);
        }

        // TODO(felix): analogs
    }

    // success
    return true;
}

/*
 * Module stuff
 */

acio::J32DModule::J32DModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("J32D", module, hookMode) {
    this->status_buffer = (uint8_t*) &STATUS_BUFFER[0];
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::J32DModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_j32d_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_j32d_update_control_status_buffer);
}
