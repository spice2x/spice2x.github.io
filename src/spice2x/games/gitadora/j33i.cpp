
#include "misc/eamuse.h"
#include "util/utils.h"
#include "util/time.h"
#include "io.h"
#include "j33i.h"


using namespace acioemu;

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

            bool picked = false;

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
                payload.buttons |= 1 << GUITAR_PICK_UP;
                picked = true;
            }

            else if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::GuitarP1PickDown]) && !picked) {
                payload.buttons |= 1 << GUITAR_PICK_DOWN;
            }

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