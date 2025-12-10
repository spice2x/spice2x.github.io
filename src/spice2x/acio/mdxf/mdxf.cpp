#include "mdxf.h"

#include "avs/game.h"
#include "games/ddr/io.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"

// constants
const size_t STATUS_BUFFER_SIZE = 32;

// static stuff
static uint8_t COUNTER_P1 = 0;
static uint8_t COUNTER_P2 = 0;

// buffers
#pragma pack(push, 1)
static struct {
    uint8_t STATUS_BUFFER_P1[7][STATUS_BUFFER_SIZE] {};
    uint8_t STATUS_BUFFER_P2[7][STATUS_BUFFER_SIZE] {};
} BUFFERS {};
#pragma pack(pop)

static bool STATUS_BUFFER_FREEZE = false;

typedef uint64_t (__cdecl *ARK_GET_TICK_TIME64_T)();

static uint64_t arkGetTickTime64() {
    static ARK_GET_TICK_TIME64_T getTickTime64 =
        (ARK_GET_TICK_TIME64_T)GetProcAddress(avs::game::DLL_INSTANCE, "arkGetTickTime64");
    if (getTickTime64 == nullptr) {
        // this works on 32-bit versions of avs, but not on 64.
        // it's better than nothing though.
        return timeGetTime();
    }
    return getTickTime64();
}

/*
 * Implementations
 */

static bool __cdecl ac_io_mdxf_get_control_status_buffer(int node, void *buffer, uint8_t a3, uint8_t a4) {

    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {

        // copy buffer
        if (node == 17 || node == 25) {
			// get buffer index
			auto i = (COUNTER_P1 + a3) % std::size(BUFFERS.STATUS_BUFFER_P1);
            memcpy(buffer, BUFFERS.STATUS_BUFFER_P1[i], STATUS_BUFFER_SIZE);
        } else if (node == 18 || node == 26) {
			// get buffer index
			auto i = (COUNTER_P2 + a3) % std::size(BUFFERS.STATUS_BUFFER_P2);
            memcpy(buffer, BUFFERS.STATUS_BUFFER_P2[i], STATUS_BUFFER_SIZE);
        } else {

            // fill with zeros on unknown node
            memset(buffer, 0, STATUS_BUFFER_SIZE);
            return false;
        }
    }

    // return success
    return true;
}

static bool __cdecl ac_io_mdxf_set_output_level(unsigned int a1, unsigned int a2, uint8_t value) {
    if (avs::game::is_model("MDX")) {
        static const struct {
            int a2[4];
        } mapping[] = {
            {
                // a1 == 17
                {
                    games::ddr::Lights::GOLD_P1_STAGE_UP_RIGHT,
                    games::ddr::Lights::GOLD_P1_STAGE_DOWN_LEFT,
                    games::ddr::Lights::GOLD_P1_STAGE_UP_LEFT,
                    games::ddr::Lights::GOLD_P1_STAGE_DOWN_RIGHT
                }
            },
            {
                // a1 == 18
                {
                    games::ddr::Lights::GOLD_P2_STAGE_UP_RIGHT,
                    games::ddr::Lights::GOLD_P2_STAGE_DOWN_LEFT,
                    games::ddr::Lights::GOLD_P2_STAGE_UP_LEFT,
                    games::ddr::Lights::GOLD_P2_STAGE_DOWN_RIGHT
                }
            }
        };
        if ((a1 == 17 || a1 == 18) && (a2 < 4)) {
            // get light from mapping
            const auto light = mapping[a1 - 17].a2[a2];

            // get lights
            auto &lights = games::ddr::get_lights();

            // write lights
            GameAPI::Lights::writeLight(RI_MGR, lights[light], value / 128.f);
        }
    }
    
    return true;
}

static bool __cdecl ac_io_mdxf_update_control_status_buffer(int node) {

    // check freeze
    if (STATUS_BUFFER_FREEZE) {
        return true;
    }

    // clear buffer
    uint8_t *buffer = nullptr;
    switch (node) {
        case 17:
        case 25:
			// increase counter
			COUNTER_P1 = (COUNTER_P1 + 1) % std::size(BUFFERS.STATUS_BUFFER_P1);
            buffer = BUFFERS.STATUS_BUFFER_P1[COUNTER_P1];
            break;
        case 18:
        case 26:
			// increase counter
			COUNTER_P2 = (COUNTER_P2 + 1) % std::size(BUFFERS.STATUS_BUFFER_P2);
            buffer = BUFFERS.STATUS_BUFFER_P2[COUNTER_P2];
            break;
        default:

            // return failure on unknown node
            return false;
    }
    memset(buffer, 0, STATUS_BUFFER_SIZE);

    // Dance Dance Revolution
    if (avs::game::is_model("MDX")) {

        // Sensor Map (LDUR):
        // FOOT DOWN = bit 32-35 = byte 4, bit 0-3
        // FOOT UP = bit 36-39 = byte 4, bit 4-7
        // FOOT RIGHT = bit 40-43 = byte 5, bit 0-3
        // FOOT LEFT = bit 44-47 = byte 5, bit 4-7
        static const size_t buttons_p1[] = {
                games::ddr::Buttons::P1_PANEL_UP,
                games::ddr::Buttons::P1_PANEL_DOWN,
                games::ddr::Buttons::P1_PANEL_LEFT,
                games::ddr::Buttons::P1_PANEL_RIGHT,
        };
        static const size_t buttons_p2[] = {
                games::ddr::Buttons::P2_PANEL_UP,
                games::ddr::Buttons::P2_PANEL_DOWN,
                games::ddr::Buttons::P2_PANEL_LEFT,
                games::ddr::Buttons::P2_PANEL_RIGHT,
        };

        // decide on button map
        const size_t *button_map = nullptr;
        switch (node) {
            case 17:
            case 25:
                button_map = &buttons_p1[0];
                break;
            case 18:
            case 26:
                button_map = &buttons_p2[0];
                break;
        }

        *(uint64_t*)&buffer[0x18] = arkGetTickTime64();

        // get buttons
        auto &buttons = games::ddr::get_buttons();

        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[0]))) {
            buffer[4] |= 0xF0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[1]))) {
            buffer[4] |= 0x0F;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[2]))) {
            buffer[5] |= 0xF0;
        }
        if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_map[3]))) {
            buffer[5] |= 0x0F;
        }
    }

    // return success
    return true;
}

/*
 * Module stuff
 */

acio::MDXFModule::MDXFModule(HMODULE module, acio::HookMode hookMode) : ACIOModule("MDXF", module, hookMode) {
    this->status_buffer = (uint8_t*) &BUFFERS;
    this->status_buffer_size = sizeof(BUFFERS);
    this->status_buffer_freeze = &STATUS_BUFFER_FREEZE;
}

void acio::MDXFModule::attach() {
    ACIOModule::attach();

    // hooks
    ACIO_MODULE_HOOK(ac_io_mdxf_get_control_status_buffer);
    ACIO_MODULE_HOOK(ac_io_mdxf_set_output_level);
    ACIO_MODULE_HOOK(ac_io_mdxf_update_control_status_buffer);
}
