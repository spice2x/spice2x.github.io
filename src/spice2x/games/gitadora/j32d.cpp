
#include "misc/eamuse.h"
#include "util/utils.h"
#include "io.h"
#include "j32d.h"


using namespace acioemu;

games::gitadora::J32DSerialDevice::J32DSerialDevice() {
    this->node_count = 1;
    log_info("gitadora", "J32D device created.");
}

bool games::gitadora::J32DSerialDevice::parse_msg(
    MessageData *msg_in,
    circular_buffer<uint8_t> *response_buffer) {

#ifdef ACIOEMU_LOG
    log_info("gitadora", "GTDR ADDR: {}, CMD: 0x{:04x}", msg_in->addr, msg_in->cmd.code);
#endif

    static uint8_t pid = 0;
    // check command
    switch (msg_in->cmd.code) {
        case ACIO_CMD_GET_VERSION: {
            // send version data
            auto msg = this->create_msg(msg_in, MSG_VERSION_SIZE);
            this->set_version(msg, 0x20a, 0, 0, 1, 0, "J32D");
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
            auto msg = this->create_msg(msg_in, 0x11);
            msg->cmd.pid = pid;
            // don't know if it's needed/correct, 
            // without it the game seems to work just fine 
            pid += 1;
            J32D_IO_STATUS payload;
            memset(&payload, 0, sizeof(J32D_IO_STATUS));
            auto &buttons = get_buttons();

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumHiTom])) {
                payload.high_tom = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumLowTom])) {
                payload.low_tom = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumSnare])) {
                payload.snare = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumFloorTom])) {
                payload.floor_tom = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumLeftCymbal])) {
                payload.left_cymbal = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumRightCymbal])) {
                payload.right_cymbal = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumHiHat]) ||
                    GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumHiHatClosed]) ||
                    GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumHiHatHalfOpen])) {
                payload.hihat = 4092;
            }

            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumBassPedal])) {
                payload.pedals |= 1;
            }
            
            if (GameAPI::Buttons::getState(RI_MGR, buttons[Buttons::DrumLeftPedal])) {
                payload.pedals |= 2; 
            }

            memcpy(msg->cmd.raw, &payload, sizeof(J32D_IO_STATUS));
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