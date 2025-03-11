#include "bi2a.h"

#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"

#include "iidx.h"
#include "io.h"

using namespace acioemu;

games::iidx::IIDXFMSerialHandle::FMSerialDevice::FMSerialDevice() {
    this->node_count = 1;
}

bool games::iidx::IIDXFMSerialHandle::FMSerialDevice::parse_msg(
    MessageData *msg_in,
    circular_buffer<uint8_t> *response_buffer
) {
#ifdef ACIOEMU_LOG
    log_info("iidx", "BI2A ADDR: {}, CMD: 0x{:04x}", msg_in->addr, msg_in->cmd.code);
#endif

    // check command
    switch (msg_in->cmd.code) {
        case ACIO_CMD_GET_VERSION: {

            // send version data
            auto msg = this->create_msg(msg_in, MSG_VERSION_SIZE);
            this->set_version(msg, 0x03, 0, 4, 2, 0, "BI2A");
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0153:
            if (DISABLE_ESPEC_IO) {
                return false;
            }
            [[fallthrough]]; // to 0x0152
        case 0x0152: { // STATUS

            // check input data length
            if (msg_in->cmd.data_size == 0x30) {

                // LED TICKER
                IIDX_LED_TICKER_LOCK.lock();
                if (!IIDXIO_LED_TICKER_READONLY)
                    memcpy(IIDXIO_LED_TICKER, &msg_in->cmd.raw[0x17], 9);
                IIDX_LED_TICKER_LOCK.unlock();

                // NEON LIGHT
                bool neon_light = msg_in->cmd.raw[0x24] != 0;
                write_top_neon(neon_light ? (uint8_t) 0xFF : (uint8_t) 0);

                // SPOT LIGHT
                uint8_t spot_light_bits = 0;
                for (size_t i = 0; i < 8; i++) {
                    auto index = i > 3 ? i + 1 : i;
                    if (msg_in->cmd.raw[0x20 + index] != 0)
                        spot_light_bits |= 1 << i;
                }
                write_top_lamp(spot_light_bits);

                // SWLED (DECK)
                uint16_t lamp_bits = 0;
                for (size_t i = 0; i < 14; i++) {
                    if (msg_in->cmd.raw[0x07 + i] != 0)
                        lamp_bits |= 1 << i;
                }
                write_lamp(lamp_bits);

                // SWLED (PANEL)
                uint8_t led_bits = 0;
                for (size_t i = 0; i < 4; i++) {
                    if (msg_in->cmd.raw[0x03 + i] != 0)
                        led_bits |= 1 << i;
                }
                write_led(led_bits);

                // flush device output
                RI_MGR->devices_flush_output();
            }

            // sleep - otherwise the IO thread will go too hard on the CPU
            Sleep(1);

            // generate message
            auto msg = this->create_msg(msg_in, 0x2E);

            // get buttons
            auto &buttons = get_buttons();

            // player 1 buttons
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_1)))
                ARRAY_SETB(msg->cmd.raw, 151);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_2)))
                ARRAY_SETB(msg->cmd.raw, 167);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_3)))
                ARRAY_SETB(msg->cmd.raw, 183);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_4)))
                ARRAY_SETB(msg->cmd.raw, 199);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_5)))
                ARRAY_SETB(msg->cmd.raw, 215);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_6)))
                ARRAY_SETB(msg->cmd.raw, 231);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_7)))
                ARRAY_SETB(msg->cmd.raw, 247);

            // player 2 buttons
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_1)))
                ARRAY_SETB(msg->cmd.raw, 263);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_2)))
                ARRAY_SETB(msg->cmd.raw, 279);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_3)))
                ARRAY_SETB(msg->cmd.raw, 295);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_4)))
                ARRAY_SETB(msg->cmd.raw, 311);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_5)))
                ARRAY_SETB(msg->cmd.raw, 327);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_6)))
                ARRAY_SETB(msg->cmd.raw, 343);
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_7)))
                ARRAY_SETB(msg->cmd.raw, 359);

            // player 1 start
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P1_Start)))
                ARRAY_SETB(msg->cmd.raw, 79);

            // player 2 start
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::P2_Start)))
                ARRAY_SETB(msg->cmd.raw, 78);

            // VEFX
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::VEFX)))
                ARRAY_SETB(msg->cmd.raw, 77);

            // EFFECT
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Effect)))
                ARRAY_SETB(msg->cmd.raw, 76);

            // service
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Service)))
                ARRAY_SETB(msg->cmd.raw, 10);

            // test
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(Buttons::Test)))
                ARRAY_SETB(msg->cmd.raw, 11);

            // turntables
            msg->cmd.raw[16] = get_tt(0, true);
            msg->cmd.raw[17] = get_tt(1, true);

            // slider
            msg->cmd.raw[0] |= get_slider(0) << 4;
            msg->cmd.raw[2] |= get_slider(1) << 4;
            msg->cmd.raw[4] |= get_slider(2) << 4;
            msg->cmd.raw[6] |= get_slider(3) << 4;
            msg->cmd.raw[7] |= get_slider(4) << 4;

            // coin
            this->coin_counter += eamuse_coin_consume_stock();
            msg->cmd.raw[8] |= this->coin_counter;

            /*
             * TODO
             * bit 9 appears to be the coin mech
             */

            // write message
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case ACIO_CMD_CLEAR:
        case ACIO_CMD_STARTUP:
        case 0x0120: // WD COMMAND
        case 0xFF: // BROADCAST
        {
            // send status 0
            auto msg = this->create_msg_status(msg_in, 0);
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        default:
            return false;
    }

    // mark as handled
    return true;
}

bool games::iidx::IIDXFMSerialHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM2") != 0) {
        return false;
    }

    log_info("iidx", "Opened COM2 (FM DEVICE)");

    // ACIO device
    acio_emu.add_device(new FMSerialDevice());

    return true;
}

int games::iidx::IIDXFMSerialHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);

    // read from emu
    DWORD bytes_read = 0;
    while (bytes_read < nNumberOfBytesToRead) {
        auto cur_byte = acio_emu.read();

        if (cur_byte.has_value()) {
            buffer[bytes_read++] = cur_byte.value();
        } else {
            break;
        }
    }

    // return amount of bytes read
    return (int) bytes_read;
}

int games::iidx::IIDXFMSerialHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);

    // write to emu
    for (DWORD i = 0; i < nNumberOfBytesToWrite; i++) {
        acio_emu.write(buffer[i]);
    }

    // return all data written
    return (int) nNumberOfBytesToWrite;
}

int games::iidx::IIDXFMSerialHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                               LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::iidx::IIDXFMSerialHandle::close() {
    log_info("iidx", "Closed COM2 (FM DEVICE)");

    return true;
}
