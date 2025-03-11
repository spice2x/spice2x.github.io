#include "hdxs.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "games/popn/io.h"
#include "games/rb/io.h"
#include "games/dea/io.h"
#include "avs/game.h"
#include "util/logging.h"
#include "util/utils.h"

using namespace GameAPI;

// state
static uint8_t STATUS_BUFFER[32] {};
static bool STATUS_BUFFER_FREEZE = false;

/*
 * Implementations
 */

static int __cdecl ac_io_hdxs_get_control_status_buffer(int a1, void *a2) {

    // copy buffer
    memcpy(a2, STATUS_BUFFER, sizeof(STATUS_BUFFER));
    return true;
}

static int __cdecl ac_io_hdxs_led_scroll(int a1, char a2, char a3, char a4, char a5, char a6, char a7, char a8, char a9,
                                         char a10, char a11, char a12, char a13) {
    return 1;
}

static int __cdecl ac_io_hdxs_led_set_pattern(int index, char r, char g, char b, uint64_t led_bits) {

    // reflec beat
    if (avs::game::is_model({"KBR", "LBR", "MBR"})) {

        // get lights
        auto &lights = games::rb::get_lights();

        // set values
        Lights::writeLight(RI_MGR, lights.at(games::rb::Lights::PoleR), r / 127.f);
        Lights::writeLight(RI_MGR, lights.at(games::rb::Lights::PoleG), g / 127.f);
        Lights::writeLight(RI_MGR, lights.at(games::rb::Lights::PoleB), b / 127.f);
    }

    // dance evolution
    if (avs::game::is_model("KDM")) {

        // get lights
        auto &lights = games::dea::get_lights();

        // decide on index
        switch (index) {
            case 12:
                Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::SideUpperLeftR), r / 127.f);
                Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::SideUpperLeftG), g / 127.f);
                Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::SideUpperLeftB), b / 127.f);
                break;
            case 14:
                Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::SideUpperRightR), r / 127.f);
                Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::SideUpperRightG), g / 127.f);
                Lights::writeLight(RI_MGR, lights.at(games::dea::Lights::SideUpperRightB), b / 127.f);
        }
    }

    // popn
    if (avs::game::is_model("M39")) {

        // mappings
        static const uint64_t top_led_bits[] = {
                0x80000000000000,
                0x40000000000000,
                0x20000000000000,
                0x10000000000000,
                0x8000000000000,
                0x4000000000000,
                0x2000000000000,
                0x1000000000000,
                0x800000000000,
                0x400000000000,
                0x200000000000,
                0x100000000000,
                0x80000000000,
                0x40000000000,
                0x20000000000,
                0x10000000000,
                0x8000000000,
                0x4000000000,
                0x2000000000,
                0x1000000000,
                0x800000000,
                0x400000000,
                0x200000000,
                0x100000000,
                0x80000000,
                0x40000000,
                0x20000000,
                0x10000000,
                0x8000000,
                0x4000000,
                0x2000000,
                0x1000000,
        };
        static const size_t light_mapping[] {
                games::popn::Lights::TopLED1,
                games::popn::Lights::TopLED2,
                games::popn::Lights::TopLED3,
                games::popn::Lights::TopLED4,
                games::popn::Lights::TopLED5,
                games::popn::Lights::TopLED6,
                games::popn::Lights::TopLED7,
                games::popn::Lights::TopLED8,
                games::popn::Lights::TopLED9,
                games::popn::Lights::TopLED10,
                games::popn::Lights::TopLED11,
                games::popn::Lights::TopLED12,
                games::popn::Lights::TopLED13,
                games::popn::Lights::TopLED14,
                games::popn::Lights::TopLED15,
                games::popn::Lights::TopLED16,
                games::popn::Lights::TopLED17,
                games::popn::Lights::TopLED18,
                games::popn::Lights::TopLED19,
                games::popn::Lights::TopLED20,
                games::popn::Lights::TopLED21,
                games::popn::Lights::TopLED22,
                games::popn::Lights::TopLED23,
                games::popn::Lights::TopLED24,
                games::popn::Lights::TopLED25,
                games::popn::Lights::TopLED26,
                games::popn::Lights::TopLED27,
                games::popn::Lights::TopLED28,
                games::popn::Lights::TopLED29,
                games::popn::Lights::TopLED30,
                games::popn::Lights::TopLED31,
                games::popn::Lights::TopLED32,
        };

        // get lights
        auto &lights = games::popn::get_lights();

        // bit scan
        for (int i = 0; i < 32; i++) {
            bool value = (led_bits & top_led_bits[i]) > 0;
            Lights::writeLight(RI_MGR, lights.at(light_mapping[i]), value ? 1.f : 0.f);
        }

        // write RGB
        auto value_r = r / 127.f;
        auto value_g = g / 127.f;
        auto value_b = b / 127.f;
        Lights::writeLight(RI_MGR, lights.at(games::popn::Lights::TopLED_R), value_r);
        Lights::writeLight(RI_MGR, lights.at(games::popn::Lights::TopLED_G), value_g);
        Lights::writeLight(RI_MGR, lights.at(games::popn::Lights::TopLED_B), value_b);
    }

    return 1;
}

static int __cdecl ac_io_hdxs_led_set_rgb_mask(int a1, char a2, char a3, long a4) {
    return 1;
}

static char __cdecl ac_io_hdxs_update_control_status_buffer(int a1) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // success
    return true;
}

static int __cdecl ac_io_hdxs_set_framing_err_packet_send_interval(int a1) {
    return a1;
}

/*
 * Module stuff
 */

acio::HDXSModule::HDXSModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("HDXS", module, hookMode) {
    this->status_buffer = STATUS_BUFFER;
    this->status_buffer_size = sizeof(STATUS_BUFFER);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::HDXSModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_hdxs_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_hdxs_led_scroll);
    ACIO_MODULE_HOOK(ac_io_hdxs_led_set_pattern);
    ACIO_MODULE_HOOK(ac_io_hdxs_led_set_rgb_mask);
    ACIO_MODULE_HOOK(ac_io_hdxs_update_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_hdxs_set_framing_err_packet_send_interval);
}
