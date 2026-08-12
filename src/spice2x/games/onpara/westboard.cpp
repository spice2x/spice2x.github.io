#include "westboard.h"

#include "io.h"
#include "util/logging.h"
#include "util/secplug.h"
#include "misc/eamuse.h"

using namespace acioemu;

games::onpara::WestBoardHandle::WestBoardDevice::WestBoardDevice() {
    /*
     * Node 1   -> Buttons
     * Node 2   -> Card Reader
     * Node 3-5 -> Lights
     */
    this->node_count = 5;
}

games::onpara::WestBoardHandle::WestBoardHandle() {
    acio_emu_.add_device(new games::onpara::WestBoardHandle::WestBoardDevice);
}

bool games::onpara::WestBoardHandle::WestBoardDevice::parse_msg(MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) {
    response_ = {};

    switch (msg_in->cmd.code) {
        case ACIO_CMD_GET_VERSION:
            set_version(&response_, ':', '^', ')', ':', '(', ":^)");
            response_.cmd.data_size = MSG_VERSION_SIZE;
            break;

        case ACIO_WESTBOARD_CMD_LOAD_SECPLUG_MODEL: 
            if (msg_in->cmd.raw[0] == SECURITY_PLUG_LICENSE) {
                secplug_model_ = secplug::encode_secplug_model("GQJC9JAA");
            }
            else if (msg_in->cmd.raw[0] == SECURITY_PLUG_ACCOUNT) {
                secplug_model_ = secplug::encode_secplug_model("@@@@@@@@");
            }
            else {
                log_warning("westboard", "invalid security plug type: {}", msg_in->cmd.raw[0]);
            }
            
            // empty response body
            response_.cmd.data_size = 0;
            break;

        case ACIO_WESTBOARD_CMD_QUERY_SECPLUG:
            // write security plug id
            response_.cmd.raw[3] = 4;
            response_.cmd.raw[4] = 3;
            response_.cmd.raw[5] = 4;
            response_.cmd.raw[6] = 3;

            // write security plug model
            std::copy(secplug_model_.begin(), secplug_model_.end(), response_.cmd.raw + 7);

            response_.cmd.data_size = 33;
            break;

        case ACIO_WESTBOARD_CMD_POLL_BUTTONS_0: 
        case ACIO_WESTBOARD_CMD_POLL_BUTTONS_1: {
            auto &buttons = games::onpara::get_buttons();
            if (GameAPI::Buttons::getState(RI_MGR, buttons[games::onpara::Buttons::Test])) {
                response_.cmd.raw[1] |= 1 << 5;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[games::onpara::Buttons::Service])) {
                response_.cmd.raw[1] |= 1 << 4;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[games::onpara::Buttons::Start])) {
                response_.cmd.raw[8] |= 1;
            }
            if (GameAPI::Buttons::getState(RI_MGR, buttons[games::onpara::Buttons::Headphone])) {
                response_.cmd.raw[8] |= 1 << 1;
            }

            response_.cmd.data_size = 15;
            break;
        }

        case ACIO_WESTBOARD_CMD_ENABLE_CARD_READER:
            // send status "1"
            response_.cmd.status = 1;
            response_.cmd.data_size = 1;
            break;

        case ACIO_WESTBOARD_CMD_POLL_CARD_READER:
            response_.cmd.data_size = 24;
            response_.cmd.raw[0] = 2;

            if (eamuse_card_insert_consume(1, 0)) {
                eamuse_get_card(1, 0, &response_.cmd.raw[2]);
            }
            break;

        case ACIO_WESTBOARD_CMD_LOAD_SECPLUG_ID:
        case ACIO_WESTBOARD_CMD_SET_LIGHTS:
            // empty response body
            response_.cmd.data_size = 0;
            break;

        case ACIO_CMD_STARTUP:
        case ACIO_CMD_CLEAR:
        case ACIO_WESTBOARD_CMD_EXECUTE: 
            // send status "0"
            response_.cmd.status = 0;
            response_.cmd.data_size = 1;
            break;

        default:
            log_warning("westboard", "unknown cmd: {}", msg_in->cmd.code);
            return false;
    }

    // set acio header
    set_header(&response_, msg_in->addr, msg_in->cmd.code, msg_in->cmd.pid, response_.cmd.data_size);
    // write message
    write_msg(&response_, response_buffer);

    return true;
}

bool games::onpara::WestBoardHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM2: baud=57600 parity=N data=8 stop=1") != 0){
        return false;
    }
    log_info("westboard", "Opened COM2 (West Board)");

    return true;
}

int games::onpara::WestBoardHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {  
    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);
    for (DWORD i = 0; i < nNumberOfBytesToWrite; i++) {
        acio_emu_.write(buffer[i]);
    }

    return nNumberOfBytesToWrite;
}

int games::onpara::WestBoardHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);

    DWORD bytes_read = 0;
    while (bytes_read < nNumberOfBytesToRead) {
        auto cur_byte = acio_emu_.read();

        if (cur_byte.has_value()) {
            buffer[bytes_read++] = cur_byte.value();
        } else {
            break;
        }
    }

    return bytes_read;
}

int games::onpara::WestBoardHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, 
                                              LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::onpara::WestBoardHandle::close() {
    log_info("westboard", "Closed COM2 (West Board)");

    return true;
}