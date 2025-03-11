#include "p3io.h"

#include "cfg/api.h"
#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"

#include "../ddr.h"
#include "../io.h"

using namespace acioemu;

games::ddr::DDRP3IOHandle::HDXSDevice::HDXSDevice() {
    this->node_count = 1;
}

bool games::ddr::DDRP3IOHandle::HDXSDevice::parse_msg(
    MessageData* msg_in,
    circular_buffer<uint8_t> *response_buffer
) {

#ifdef ACIOEMU_LOG
    log_info("ddr", "HDXS ADDR: {}, CMD: 0x{:x}", msg_in->addr, msg_in->cmd.code);
#endif

    // handle command
    switch (msg_in->cmd.code) {
        case ACIO_CMD_GET_VERSION: {

            // send version data
            auto msg = this->create_msg(msg_in, MSG_VERSION_SIZE);
            this->set_version(msg, 0x204, 0, 1, 6, 0, "HDXS");
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case ACIO_CMD_KEEPALIVE: {

            // send empty message
            auto msg = this->create_msg(msg_in, 0);
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0112: { // LED
            static const size_t hd_button_static_mapping[] {
                    Lights::HD_P1_START,
                    Lights::HD_P1_UP_DOWN,
                    Lights::HD_P1_LEFT_RIGHT,
                    Lights::HD_P2_START,
                    Lights::HD_P2_UP_DOWN,
                    Lights::HD_P2_LEFT_RIGHT,
            };
            static const size_t hd_panel_static_mapping[] {
                    Lights::HD_P1_SPEAKER_F_G,
                    Lights::HD_P1_SPEAKER_F_R,
                    Lights::HD_P1_SPEAKER_F_B,
                    Lights::HD_P2_SPEAKER_F_G,
                    Lights::HD_P2_SPEAKER_F_R,
                    Lights::HD_P2_SPEAKER_F_B,
                    Lights::HD_P1_SPEAKER_W_G,
                    Lights::HD_P1_SPEAKER_W_R,
                    Lights::HD_P1_SPEAKER_W_B,
                    Lights::HD_P2_SPEAKER_W_G,
                    Lights::HD_P2_SPEAKER_W_R,
                    Lights::HD_P2_SPEAKER_W_B,
            };

            // get lights
            auto &lights = games::ddr::get_lights();

            // check to see mode at runtime.
            if (!games::ddr::SDMODE) {
                const auto &data = &msg_in->cmd.raw[1];

                // button LEDs
                for (size_t i = 0; i < std::size(hd_button_static_mapping); i++) {
                    const float value = (data[i] & 0x80) ? 1.f : 0.f;
                    GameAPI::Lights::writeLight(RI_MGR, lights[hd_button_static_mapping[i]], value);
                }

                // speaker LEDs
                for (size_t i = 0; i < std::size(hd_panel_static_mapping) / 3; i++) {
                    const size_t light_index = i * 3;
                    const float g = static_cast<float>(data[light_index + 0] & 0x7f) / 127.f;
                    const float r = static_cast<float>(data[light_index + 1] & 0x7f) / 127.f;
                    const float b = static_cast<float>(data[light_index + 2] & 0x7f) / 127.f;
                    GameAPI::Lights::writeLight(RI_MGR, lights[hd_panel_static_mapping[light_index + 0]], g);
                    GameAPI::Lights::writeLight(RI_MGR, lights[hd_panel_static_mapping[light_index + 1]], r);
                    GameAPI::Lights::writeLight(RI_MGR, lights[hd_panel_static_mapping[light_index + 2]], b);
                }
            }

            // flush
            RI_MGR->devices_flush_output();

            // send status 0
            auto msg = this->create_msg_status(msg_in, 0x00);
            write_msg(msg, response_buffer);
            delete msg;

            break;
        }
        case ACIO_CMD_CLEAR:
        case ACIO_CMD_STARTUP:
        case 0x0110: // ???
        case 0x0128: // ???
        case 0xFF: // BROADCAST
        {
            // send status 0
            auto msg = this->create_msg_status(msg_in, 0x00);
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

void games::ddr::DDRP3IOHandle::write_msg(const uint8_t *data, size_t len) {
    read_buf.put(0xAA);
    read_buf.put((uint8_t) len);
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        if (b == 0xAA || b == 0xFF) {
            read_buf.put(0xFF);
            b = ~b;
        }
        read_buf.put(b);
    }
}

bool games::ddr::DDRP3IOHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"\\\\.\\P3IO\\p3io") != 0)
        return false;

    if (!acio_emu) {
        acio_emu = new acioemu::ACIOEmu();
        // DO NOT CHANGE THE ORDER OF THESE
        // (until ICCA is split into separate devices with individual unit ids)
        acio_emu->add_device(new acioemu::ICCADevice(false, true, 2));
        acio_emu->add_device(new HDXSDevice());
    }

    log_info("ddr", "Opened P3IO");

    return true;
}

int games::ddr::DDRP3IOHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);

    // read from emu
    DWORD bytes_read = 0;
    while (!read_buf.empty() && bytes_read < nNumberOfBytesToRead) {
        buffer[bytes_read++] = read_buf.get();
    }

    // this mustn't happen ever
    if (bytes_read == 0) {
        log_fatal("ddr", "p3io received no data");
    }

    // return amount of bytes read
    return (int) bytes_read;
}

int games::ddr::DDRP3IOHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);

    // check header
    if (nNumberOfBytesToWrite < 2 || buffer[0] != 0xAA || buffer[1] > 0x7F) {
        log_warning("ddr", "p3io has the wrong header: {}", bin2hex(buffer, (int) nNumberOfBytesToWrite));

        return (int) nNumberOfBytesToWrite;
    }

    // parse data
    std::vector<uint8_t> parsed;
    for (DWORD i = 2; i < nNumberOfBytesToWrite; i++) {
        uint8_t b = buffer[i];

        if (b == 0xFFu && i + 1 < nNumberOfBytesToWrite) {
            b = ~buffer[++i];
        }

        parsed.push_back(b);
    }

    // check message size
    if (parsed.size() != buffer[1]) {
        log_warning("ddr", "p3io has wrong message size: {}/{}", parsed.size(), (int) buffer[1]);
        return (int) nNumberOfBytesToWrite;
    }

    // check command
    switch (parsed[1]) {
        case 0x01: { // VERSION
            uint8_t data[8];
            memset(data, 0, 8);

            // device code
            strncpy((char*) &data[2], "JDX", 4);

            // write message
            write_msg(data, sizeof(data));
            break;
        }
        case 0x05: { // WATCHDOG
            uint8_t data[] = {0x00, 0x00};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x24: { // SET LIGHTS

            // hd mappings
            static const size_t hd_mapping_bits[] {
                    0x1000000, // HD SPOT RED
                    0x2000000, // HD SPOT BLUE
                    0x4000000, // HD TOP SPOT RED
                    0x8000000, // HD TOP SPOT BLUE
            };
            static const size_t hd_mapping[] {
                    Lights::SPOT_RED,
                    Lights::SPOT_BLUE,
                    Lights::TOP_SPOT_RED,
                    Lights::TOP_SPOT_BLUE
            };

            // sd mappings, bits are reused according to gamemode
            static const size_t sd_mapping_bits[] {
                    0x01000000, // SD P1 BUTTON
                    0x02000000, // SD P2 BUTTON
                    0x40000000, // SD P1 HALOGEN LOWER
                    0x80000000, // SD P1 HALOGEN UPPER
                    0x10000000, // SD P2 HALOGEN LOWER
                    0x20000000, // SD P2 HALOGEN UPPER
            };
            static const size_t sd_mapping[] {
                    Lights::P1_BUTTON,
                    Lights::P2_BUTTON,
                    Lights::P1_HALOGEN_LOWER,
                    Lights::P1_HALOGEN_UPPER,
                    Lights::P2_HALOGEN_LOWER,
                    Lights::P2_HALOGEN_UPPER
            };

            static const size_t hd_to_sd_mapping[] {
                    Lights::P1_HALOGEN_LOWER,
                    Lights::P1_HALOGEN_UPPER,
                    Lights::P2_HALOGEN_LOWER,
                    Lights::P2_HALOGEN_UPPER
            };

            // get light bits
            uint32_t light_bits = *reinterpret_cast<uint32_t *>(&parsed[3]);

            // get lights
            auto &lights = get_lights();

            // check to see mode at runtime.
            if (games::ddr::SDMODE) {

                // bit scan
                for (size_t i = 0; i < 6; i++) {
                    float value = (light_bits & sd_mapping_bits[i]) ? 1.f : 0.f;
                    GameAPI::Lights::writeLight(RI_MGR, lights[sd_mapping[i]], value);
                }
            } else {

                // bit scan
                for (size_t i = 0; i < 4; i++) {
                    float value = (light_bits & hd_mapping_bits[i]) ? 1.f : 0.f;
                    GameAPI::Lights::writeLight(RI_MGR, lights[hd_mapping[i]], value);

                    // perform HD->SD light mappings for older cabinet styles.
                    GameAPI::Lights::writeLight(RI_MGR, lights[hd_to_sd_mapping[i]], value);
                }

                // use both sat spots for a neon pulse
                float value_neon = (light_bits & hd_mapping_bits[0] && hd_mapping_bits[1]) ? 1.f : 0.f;
                GameAPI::Lights::writeLight(RI_MGR, lights[Lights::NEON], value_neon);
            }

            // flush
            RI_MGR->devices_flush_output();

            uint8_t data[] = {0x00, 0x00};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x25: { // SEC PLUG
            uint8_t data[43] {};

            // plug present
            data[2] = 1;

            // copy data
            if (parsed[2] & 0x10) {
                static const uint8_t black_data[] {
                        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
                        0xBE, 0xB5, 0xB2, 0xAC, 0x16, 0x8C, 0xE7, 0xA8,
                        0x92, 0xB8, 0x1A, 0x86, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF7
                };
                memcpy(&data[3], black_data, sizeof(black_data));
            } else {
                static const uint8_t white_data[] {
                        0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
                        0xC3, 0xD4, 0x45, 0xE8, 0x7C, 0x17, 0x20, 0x08,
                        0x82, 0x20, 0x08, 0x82, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1B
                };
                memcpy(&data[3], white_data, sizeof(white_data));
            }

            // write data
            write_msg(data, sizeof(data));
            break;
        }
        case 0x27: { // CABINET TYPE
            if (games::ddr::SDMODE) {
                uint8_t data[] = {0x00, 0x00};
                write_msg(data, sizeof(data));
            } else {
                uint8_t data[] = {0x00, 0x01};
                write_msg(data, sizeof(data));
            }
            break;
        }
        case 0x29: { // VIDEO FREQUENCY
            uint8_t data[] = {0x00, 0x00, 0x00};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x2B: { // ???
            uint8_t data[] = {0x00, 0x00};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x2F: { // MODE
            uint8_t data[] = {0x00, 0x00};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x31: { // COIN STOCK
            uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            // TODO
            write_msg(data, sizeof(data));
            break;
        }
        case 0x32: { // ???
            uint8_t data[] = {0x00, 0x00};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x38: { // PORT OPERATION
            uint8_t op = parsed[3];
            uint8_t port = parsed[2];
            //uint8_t baud = parsed[4];

            // open port
            if (op == 0x00) {
                log_info("ddr", "opened p3io remote port #{}", (int) port);

                uint8_t data[] = {0x00, 0x00, 0x00};
                write_msg(data, sizeof(data));
                break;
            }

            // close port
            if (op == 0xFF) {
                log_info("ddr", "closed p3io remote port #{}", (int) port);

                uint8_t data[] = {0x00, 0x00, 0x00};
                write_msg(data, sizeof(data));
                break;
            }

            // error
            uint8_t data[] = {0x00, 0x00, 0xFF};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x3A: { // REMOTE PORT WRITE
            uint8_t len = parsed[3];

            // check length
            if (len > parsed.size() - 4) {
                log_fatal("ddr", "p3io remote port data too small");
            }

            // pass data to ACIO
            for (int i = 4; i < len + 4; i++) {
                acio_emu->write(parsed[i]);
            }

            // no error
            uint8_t data[] = {0x00, 0x00, len};
            write_msg(data, sizeof(data));
            break;
        }
        case 0x3B: { // REMOTE PORT READ
            uint8_t len = parsed[3];

            // build msg data
            std::vector<uint8_t> msg_data;
            msg_data.reserve(static_cast<size_t>(len) + 3);
            msg_data.push_back(0x00);
            msg_data.push_back(0x00);

            // placeholder for ACIO message size
            msg_data.push_back(len);

            // read data from ACIO
            uint8_t acio_len = 0;
            while (acio_len < len && acio_len < 0xFF) {
                auto cur_byte = acio_emu->read();

                if (cur_byte.has_value()) {
                    msg_data.push_back(cur_byte.value());
                    acio_len++;
                } else {
                    break;
                }
            }

            // update placeholder with actual length
            msg_data[2] = acio_len;

            // write message
            write_msg(msg_data.data(), msg_data.size());
            break;
        }
        default: {
            log_fatal("ddr", "p3io unknown command: 0x{:x}", parsed[1]);
        }
    }

    // return all data written
    return (int) nNumberOfBytesToWrite;
}

int games::ddr::DDRP3IOHandle::device_io(
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize
) {

    // check buffer size
    if (nOutBufferSize >= 4) {

        // cool down
        Sleep(1);

        // get controls as single variable (4 bytes)
        auto &controls = *(uint32_t*) lpOutBuffer;

        // reset
        controls = 0;

        /*
         * P3IO DDR Bit Mappings
         * 4 bytes, from low to high order (0-31)
         * all bits represent the inverted state
         *
         *
         * 30 - SERVICE
         * 28 - TEST
         * 29 - COIN MECH
         * 8  - P1 START
         * 9  - P1 PANEL UP
         * 10 - P1 PANEL DOWN
         * 11 - P1 PANEL LEFT
         * 12 - P1 PANEL RIGHT
         * 24 - P1 MENU UP
         * 25 - P1 MENU DOWN
         * 14 - P1 MENU LEFT
         * 15 - P1 MENU RIGHT
         * 16 - P2 START
         * 17 - P2 PANEL UP
         * 18 - P2 PANEL DOWN
         * 19 - P2 PANEL LEFT
         * 20 - P2 PANEL RIGHT
         * 26 - P2 MENU UP
         * 27 - P2 MENU DOWN
         * 22 - P2 MENU LEFT
         * 23 - P2 MENU RIGHT
         */

        // shift table
        static size_t shift_table[] = {
                30, 28, 29, 8, 9, 10, 11, 12, 24, 25, 14, 15, 16, 17, 18, 19, 20, 26, 27, 22, 23
        };
        static size_t button_table[] = {
                Buttons::SERVICE,
                Buttons::TEST,
                Buttons::COIN_MECH,
                Buttons::P1_START,
                Buttons::P1_PANEL_UP,
                Buttons::P1_PANEL_DOWN,
                Buttons::P1_PANEL_LEFT,
                Buttons::P1_PANEL_RIGHT,
                Buttons::P1_MENU_UP,
                Buttons::P1_MENU_DOWN,
                Buttons::P1_MENU_LEFT,
                Buttons::P1_MENU_RIGHT,
                Buttons::P2_START,
                Buttons::P2_PANEL_UP,
                Buttons::P2_PANEL_DOWN,
                Buttons::P2_PANEL_LEFT,
                Buttons::P2_PANEL_RIGHT,
                Buttons::P2_MENU_UP,
                Buttons::P2_MENU_DOWN,
                Buttons::P2_MENU_LEFT,
                Buttons::P2_MENU_RIGHT,
        };

        // update states
        auto &buttons = get_buttons();
        size_t count = 0;
        for (auto shift : shift_table) {
            if (GameAPI::Buttons::getState(RI_MGR, buttons.at(button_table[count++]))) {
                controls |= 1 << shift;
            }
        }

        // invert controls
        controls = ~controls;

        // return data size
        return 4;
    }

    // fail
    return -1;
}

bool games::ddr::DDRP3IOHandle::close() {
    log_info("ddr", "Closed P3IO");
    return true;
}
