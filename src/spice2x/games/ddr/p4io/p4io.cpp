#include "p4io.h"

#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"

#include "../io.h"

#include <algorithm>

#ifndef FILE_DEVICE_UNKNOWN
#define FILE_DEVICE_UNKNOWN 0x00000022
#endif

// Packet header seems to be
// AA cmdId Seq maybe_length?

namespace {
#pragma pack(push, 1)
    enum P4IOIoCtl : uint16_t {
        P4IO_IOCTL_GET_INPUTS = 0x801,
        P4IO_IOCTL_GET_VERSION_FUNC = 0x803,
    };

    enum class P4IOCmd : uint8_t {
        Nop = 0x00,
        GetTypeVerProd = 0x01,
        UpdateLights = 0x12,
        CoinHandler = 0x18,
        Unk_1C = 0x1C,
        IoSciOpen = 0x20,
        SciTransfer = 0x21,
        IoSciClose = 0x24,
        ReadSecPlug_01 = 0x40,
        ReadSecPlug_02 = 0x41,
    };

    struct P4IOHeader {
        uint8_t magic;
        P4IOCmd cmd;
        uint8_t seq;
        uint8_t length;
    };
#pragma pack(pop)

    const uint8_t blackPlugData[0x20] = {
        0xC9, 0xFE, 0xC1, 0x5B, 0x8D, 0x8A, 0xE7, 0xA8,
        0x92, 0xB8, 0x1A, 0x86, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77
    };

    const uint8_t whitePlugData[0x20] = {
        0x18, 0x98, 0x12, 0x71, 0xB3, 0xA2, 0x20, 0x08,
        0x82, 0x20, 0x08, 0x82, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDA
    };
}

namespace games::ddr {
    bool DDRP4IOHandle::open(LPCWSTR lpFileName) {
        if (wcscmp(lpFileName, L"\\\\.\\P4IO\\p4io") != 0) {
            return false;
        }

        if (!m_acio_emu) {
            m_acio_emu = new acioemu::ACIOEmu();
            m_acio_emu->add_device(new acioemu::ICCADevice(false, true, 2));
            m_acio_emu->add_device(new HDXSDevice());
        }

        log_info("ddr", "Opened P4IO");

        return true;
    }

    int DDRP4IOHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
        DWORD transferSize = std::min(nNumberOfBytesToRead, (DWORD)m_read_buf.size());
        memcpy(lpBuffer, m_read_buf.data(), transferSize);
        m_read_buf.erase(m_read_buf.begin(), m_read_buf.begin()+transferSize);

        return transferSize;
    }

    int DDRP4IOHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
        const P4IOHeader *header = reinterpret_cast<const P4IOHeader*>(lpBuffer);
        const uint8_t *data = reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(lpBuffer)+sizeof(P4IOHeader));

        if(nNumberOfBytesToWrite < sizeof(P4IOHeader)
            || header->magic != 0xAA
            || nNumberOfBytesToWrite < sizeof(P4IOHeader)+header->length)
        {
            log_warning("ddr", "p4io has the wrong header: {}", bin2hex(reinterpret_cast<const uint8_t*>(lpBuffer),
                (int)nNumberOfBytesToWrite));

            return 0;
        }

        std::vector<uint8_t> responseBuffer(64);
        P4IOHeader *respHeader = reinterpret_cast<P4IOHeader*>(responseBuffer.data());
        respHeader->magic = 0xAA;
        respHeader->cmd = header->cmd;
        respHeader->seq = header->seq;
        uint8_t *respData = responseBuffer.data()+sizeof(P4IOHeader);

        switch(header->cmd)
        {
            case P4IOCmd::Nop:
            case P4IOCmd::Unk_1C:
            case P4IOCmd::IoSciOpen:
            case P4IOCmd::IoSciClose:
            {
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                return nNumberOfBytesToWrite;
            }
            break;
            case P4IOCmd::GetTypeVerProd: {
                respHeader->length = 0x2C;

                // type
                memcpy(&respData[0], "JDX", 4);

                // version
                respData[5] = 69;
                respData[6] = 69;
                respData[7] = 69;

                // prod
                memcpy(&respData[8], "prod", 4);

                // date
                memcpy(&respData[0xC], "2024-03-12", 10);

                // time
                memcpy(&respData[0x1C], "06:39:21", 8);

                m_read_buf_mutex.lock();
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                m_read_buf_mutex.unlock();
                return nNumberOfBytesToWrite;
            }
            break;
            case P4IOCmd::UpdateLights: {
                static const size_t lightMapping[] = {
                    Lights::WHITE_SPEAKER_TOP_G,
                    Lights::WHITE_SPEAKER_TOP_R,
                    Lights::WHITE_SPEAKER_TOP_B,
                    Lights::WHITE_SPEAKER_BOTTOM_G,
                    Lights::WHITE_SPEAKER_BOTTOM_R,
                    Lights::WHITE_SPEAKER_BOTTOM_B,
                    Lights::WHITE_WOOFER_G,
                    Lights::WHITE_WOOFER_R,
                    Lights::WHITE_WOOFER_B,
                    Lights::HD_P1_START,
                    Lights::HD_P1_UP_DOWN,
                    Lights::HD_P1_LEFT_RIGHT
                };

                auto &lights = get_lights();

                for(size_t i = 0; i < std::size(lightMapping); ++i) {
                    GameAPI::Lights::writeLight(RI_MGR, lights[lightMapping[i]], data[i] == 0 ? 0.f : 1.f);
                }

                RI_MGR->devices_flush_output();

                m_read_buf_mutex.lock();
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                m_read_buf_mutex.unlock();
                return nNumberOfBytesToWrite;
            }
            break;
            case P4IOCmd::CoinHandler: {
                respHeader->length = 0x04;

                // TODO:
                m_read_buf_mutex.lock();
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                m_read_buf_mutex.unlock();
                return nNumberOfBytesToWrite;
            }
            break;
            case P4IOCmd::SciTransfer: {
                // request is 1 byte port #, followed by data

                if(header->length > 1 && data[0] == 0) {
                    for(size_t i = 0; i < header->length-1u; ++i) {
                        m_acio_emu->write(data[1+i]);
                    }
                }

                respHeader->length = 2;

                //response is 1 byte port #, 1 byte error flag, followed by data
                if(header->length >= 1 && data[0] == 0) {
                    uint8_t readBytes = 0;

                    while(readBytes < 58) {
                        const auto b = m_acio_emu->read();

                        if(!b.has_value()) {
                            break;
                        }

                        respData[2+readBytes] = b.value();
                        ++readBytes;
                    }

                    respHeader->length = 2+readBytes;
                }

                m_read_buf_mutex.lock();
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                m_read_buf_mutex.unlock();
                return nNumberOfBytesToWrite;
            }
            break;
            case P4IOCmd::ReadSecPlug_01: {
                // maybe just a presense check

                respHeader->length = 0x09;
                memset(&respData[1], 0xAA, 8);

                m_read_buf_mutex.lock();
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                m_read_buf_mutex.unlock();
                return nNumberOfBytesToWrite;
            }
            break;
            case P4IOCmd::ReadSecPlug_02: {
                respHeader->length = 0x21;

                if(data[0] == 0) {
                    memcpy(&respData[1], blackPlugData, sizeof(blackPlugData));
                } else {
                    memcpy(&respData[1], whitePlugData, sizeof(whitePlugData));
                }

                m_read_buf_mutex.lock();
                m_read_buf.insert(m_read_buf.end(), responseBuffer.begin(), responseBuffer.end());
                m_read_buf_mutex.unlock();
                return nNumberOfBytesToWrite;
            }
            break;
        }

        return 0;
    }

    int DDRP4IOHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                                 DWORD nOutBufferSize) {
        if(((dwIoControlCode>>16)&0xFFFF) == FILE_DEVICE_UNKNOWN) {
            switch((dwIoControlCode>>2)&0x3FFF) {
                case P4IO_IOCTL_GET_INPUTS: {
                    memset(lpOutBuffer, 0, 16);
                    auto controls = (uint32_t*) lpOutBuffer;

                    const std::pair<size_t, uint8_t> buttonMapping[] =
                    {
                        {Buttons::SERVICE, 25}, //Good
                        {Buttons::TEST, 28}, //Good
                        {Buttons::COIN_MECH, 24}, //Good
                        {Buttons::P1_START, 0}, //Good
                        {Buttons::P1_PANEL_UP, 5}, //Good
                        {Buttons::P1_PANEL_DOWN, 6}, //Good
                        {Buttons::P1_PANEL_LEFT, 7}, //Goood
                        {Buttons::P1_PANEL_RIGHT, 16}, //Good
                        {Buttons::P1_MENU_UP, 1}, //Good
                        {Buttons::P1_MENU_DOWN, 2}, //Good
                        {Buttons::P1_MENU_LEFT, 3}, //Good
                        {Buttons::P1_MENU_RIGHT, 4}, //Good
                        {Buttons::P2_START, 8}, //Good
                        {Buttons::P2_PANEL_UP, 13}, //Good
                        {Buttons::P2_PANEL_DOWN, 14}, //Good
                        {Buttons::P2_PANEL_LEFT, 15}, //Good
                        {Buttons::P2_PANEL_RIGHT, 20}, //Good
                        {Buttons::P2_MENU_UP, 9}, //Good
                        {Buttons::P2_MENU_DOWN, 10}, //Good
                        {Buttons::P2_MENU_LEFT, 11}, //Good
                        {Buttons::P2_MENU_RIGHT, 12}, //Good
                    };

                    auto &buttons = get_buttons();
                    for(const auto &mapping : buttonMapping)
                    {
                        if(GameAPI::Buttons::getState(RI_MGR, buttons[mapping.first]))
                        {
                            controls[mapping.second/32] |= 1<<(mapping.second%32);
                        }
                    }

                    return 16;
                }
                break;
                case P4IO_IOCTL_GET_VERSION_FUNC: {
                    if(nOutBufferSize > strlen("spicy p4io") + 1) {
                        strcpy((char*)lpOutBuffer, "spicy p4io");
                        return 11;
                    }
                }
                break;
            }
        }

        return 0;
    }

    bool DDRP4IOHandle::close() {
        log_info("ddr", "Closed P4IO");
        return true;
    }

    DDRP4IOHandle::HDXSDevice::HDXSDevice() {
        this->node_count = 1;
    }

    bool DDRP4IOHandle::HDXSDevice::parse_msg(
        acioemu::MessageData* msg_in,
        circular_buffer<uint8_t> *response_buffer) {
        switch(msg_in->cmd.code)
        {
            case acioemu::ACIO_CMD_GET_VERSION: {
                auto msg = this->create_msg(msg_in, acioemu::MSG_VERSION_SIZE);
                this->set_version(msg, 0x204, 0, 1, 6, 0, "HDXS");
                write_msg(msg, response_buffer);
                delete msg;
            }
            break;
            case acioemu::ACIO_CMD_KEEPALIVE: {
                auto msg = this->create_msg(msg_in, 0);
                write_msg(msg, response_buffer);
                delete msg;
                break;
            }
            break;
            case acioemu::ACIO_CMD_STARTUP:
            case acioemu::ACIO_CMD_CLEAR:
            case 0x110:
            case 0x128:
            {
                auto msg = this->create_msg_status(msg_in, 0x00);
                write_msg(msg, response_buffer);
                delete msg;
            }
            break;
            default:
                log_info("ddr", "HDXS unhandled {:03X}", msg_in->cmd.code);
        }
        return true;
    }
}