
#include "misc/eamuse.h"
#include "util/utils.h"
#include "util/time.h"
#include "io.h"
#include "j33i.h"


#define GUITAR_BTN_R 0x7
#define GUITAR_BTN_G 0x6
#define GUITAR_BTN_B 0x5
#define GUITAR_BTN_Y 0x4
#define GUITAR_BTN_P 0x3
#define GUITAR_PICK_DOWN 0xC
#define GUITAR_PICK_UP 0xD

using namespace acioemu;

// why this number?? no clue, but this is what the test menu shows as the max
// value in the green box with the red dot; the value display itself can go
// higher but the red dot will reach the edge at +-189
#define ARENA_MODEL_WAIL_MAX 189

static bool GFDM_GF_PICK_STATE_UP;
static bool GFDM_GF_PICK_STATE_DOWN;

games::gitadora::J33ISerialDevice::J33ISerialDevice() {
    this->node_count = 1;
    log_info("gitadora", "J33I device created.");
}

bool games::gitadora::J33ISerialDevice::parse_msg(
    MessageData *msg_in,
    circular_buffer<uint8_t> *response_buffer) {

#ifdef ACIOEMU_LOG
    log_info("gitadora", "GTDR ADDR: {}, CMD: 0x{:04x}", msg_in->addr, msg_in->cmd.code);
#endif

    static uint8_t pid = 0;
    // check command
    switch (msg_in->cmd.code)
    {
        case ACIO_CMD_GET_VERSION: {
            // send version data
            auto msg = this->create_msg(msg_in, MSG_VERSION_SIZE);
            this->set_version(msg, 0x10a, 0, 1, 0, 2, "J33I");
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case ACIO_CMD_STARTUP:
        case ACIO_CMD_CLEAR:
        case 0x0120: {
            if (msg_in->cmd.code == 0x0120) {
                pid = msg_in->cmd.pid;
                this->is_ready = true;
            }
            // send status 0
            auto msg = this->create_msg_status(msg_in, 0);
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        // IO
        case 0x012F: {
            auto msg = this->create_msg(msg_in, 0x8);
            msg->cmd.pid = pid;
            pid += 1;
            J33I_IO_STATUS payload;
            memset(&payload, 0, sizeof(J33I_IO_STATUS));

            auto &buttons = get_buttons();
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1R])) {
                payload.buttons |= 1 << GUITAR_BTN_R;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1G])) {
                payload.buttons |= 1 << GUITAR_BTN_G;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1B])) {
                payload.buttons |= 1 << GUITAR_BTN_B;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1Y])) {
                payload.buttons |= 1 << GUITAR_BTN_Y;
            }   

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1P])) {
                payload.buttons |= 1 << GUITAR_BTN_P;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1PickUp])) {
                if (!GFDM_GF_PICK_STATE_UP) {
                    GFDM_GF_PICK_STATE_UP = true;
                    payload.buttons |= 1 << GUITAR_PICK_UP;
                }
            } else {
                GFDM_GF_PICK_STATE_UP = false;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1PickDown])) {
                if (!GFDM_GF_PICK_STATE_DOWN) {
                    GFDM_GF_PICK_STATE_DOWN = true;
                    payload.buttons |= 1 << GUITAR_PICK_DOWN;
                }
            } else {
                GFDM_GF_PICK_STATE_DOWN = false;
            }

            auto &analogs = get_analogs();

            // get x,y,z analog values [-0.5f, 0.5f], centered at 0.f
            float x = 0.f;
            auto x_analog = analogs[Analogs::GuitarP1WailX];
            if (x_analog.isSet()) {
                x = GameAPI::Analogs::getState(RI_MGR, x_analog) - 0.5f;
            }

            float y = 0.f;
            auto y_analog = analogs[Analogs::GuitarP1WailY];
            if (y_analog.isSet()) {
                y = GameAPI::Analogs::getState(RI_MGR, y_analog) - 0.5f;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1WailUp])) {
                y = -0.5f;
            } else if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1WailDown])) {
                y = 0.5f;
            }

            float z = 0.f;
            auto z_analog = analogs[Analogs::GuitarP1WailZ];
            if (z_analog.isSet()) {
                z = GameAPI::Analogs::getState(RI_MGR, z_analog) - 0.5f;
            }

            // convert x,y,z to unsigned integer values that the game expects
            const uint16_t x_int = x * ARENA_MODEL_WAIL_MAX * 2;
            const uint16_t y_int = y * ARENA_MODEL_WAIL_MAX * 2;
            const uint16_t z_int = z * ARENA_MODEL_WAIL_MAX * 2;
            payload.gyroscope_x = static_cast<uint16_t>(((x_int >> 3) & (0xFF)) | ((x_int & 0x07) << 12));
            payload.gyroscope_y = static_cast<uint16_t>(((y_int >> 3) & (0xFF)) | ((y_int & 0x07) << 12));
            payload.gyroscope_z = static_cast<uint16_t>(((z_int >> 3) & (0xFF)) | ((z_int & 0x07) << 12));

            memcpy(msg->cmd.raw, &payload, sizeof(J33I_IO_STATUS));
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
    default:
#ifdef ACIOEMU_LOG
        log_info("gitadora", "Unhandled cmd {} received.", msg_in->cmd.code);
#endif
        return false;
    }

    // mark as handled
    return true;
}