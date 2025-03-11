#include "ezusb.h"

#include "rawinput/rawinput.h"
#include "util/logging.h"

#include "iidx.h"
#include "io.h"

bool games::iidx::EZUSBHandle::open(LPCWSTR lpFileName) {
    return wcscmp(lpFileName, L"\\\\.\\Ezusb-0") == 0;
}

int games::iidx::EZUSBHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    log_warning("iidx", "EZUSB invalid read operation!");
    return -1;
}

int games::iidx::EZUSBHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    log_warning("iidx", "EZUSB invalid write operation!");
    return -1;
}

int games::iidx::EZUSBHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
        LPVOID lpOutBuffer, DWORD nOutBufferSize) {

    // initialize check
    if (!init) {
        // TODO: initialize input
        init = true;
        init_success = true;
    }

    if (init_success) {
        switch (dwIoControlCode) {

            // device descriptor
            case 0x222004:

                // check output buffer size
                if (nOutBufferSize >= 18) {

                    // set descriptor data
                    *((char *) lpOutBuffer + 8) = 0x47;
                    *((char *) lpOutBuffer + 9) = 0x05;
                    *((char *) lpOutBuffer + 10) = 0x35;
                    *((char *) lpOutBuffer + 11) = 0x22;

                    // return output buffer size
                    return 18;

                } else // buffer too small
                    return -1;

                // vendor
            case 0x222014:

                // check input buffer size
                if (nInBufferSize >= 10)
                    return 0;
                else
                    return -1;

                // data read
            case 0x22204E:

                // check input buffer size
                if (nInBufferSize < 4)
                    return -1;

                // read 0x01
                if (*(DWORD *) lpInBuffer == 0x01) {
                    *((DWORD *) lpOutBuffer) = get_pad();
                    *((char *) lpOutBuffer + 4) = FPGA_CMD_STATE;
                    *((char *) lpOutBuffer + 7) = get_tt(1, false);
                    *((char *) lpOutBuffer + 8) = get_tt(0, false);
                    *((char *) lpOutBuffer + 9) = FPGA_COUNTER++;
                    *((char *) lpOutBuffer + 11) = 1; // 1 success; 0 failed
                    *((char *) lpOutBuffer + 13) = get_slider(1) << 4 | get_slider(0);
                    *((char *) lpOutBuffer + 14) = get_slider(3) << 4 | get_slider(2);
                    *((char *) lpOutBuffer + 15) = get_slider(4);
                    return (int) nOutBufferSize;
                }

                // read 0x03
                if (*(DWORD *) lpInBuffer == 0x03) {

                    // check output buffer size
                    if (nOutBufferSize < 64)
                        return -1;

                    // null node read
                    if (FPGA_CUR_NODE == 0) {
                        *(int *) lpOutBuffer = 0;
                        return (int) nOutBufferSize;
                    }

                    // check node size
                    if (FPGA_CUR_NODE != 64)
                        return -1;

                    // check command
                    if (SRAM_CMD != 2)
                        return -1;

                    // check if page exists
                    if (SRAM_PAGE >= 12)
                        return -1;

                    // write page
                    *((char *) lpOutBuffer + 1) = (char) SRAM_PAGE;

                    // copy page
                    memcpy((uint8_t*) lpOutBuffer + 2, SRAM_DATA + 62 * SRAM_PAGE, 62);
                    SRAM_PAGE++;

                    // write node
                    *((char *) lpOutBuffer) = (char) FPGA_CUR_NODE;

                    return (int) nOutBufferSize;
                }

                // unknown
                return -1;

                // data write
            case 0x222051:

                // check in buffer size
                if (nInBufferSize < 4)
                    return -1;

                // write 0x00
                if (*(DWORD *) lpInBuffer == 0x00) {

                    // check out buffer size
                    if (nOutBufferSize < 10)
                        return -1;

                    // get data
                    uint16_t lamp = *((uint16_t *) lpOutBuffer + 0);
                    uint8_t led = *((uint8_t *) lpOutBuffer + 6);
                    uint8_t top_lamp = *((uint8_t *) lpOutBuffer + 8);
                    uint8_t top_neon = *((uint8_t *) lpOutBuffer + 9);

                    // process data
                    write_lamp(lamp);
                    write_led(led);
                    write_top_lamp(top_lamp);
                    write_top_neon(top_neon);

                    // flush device output
                    RI_MGR->devices_flush_output();

                    char write_node = *((char *) lpOutBuffer + 2);
                    if (write_node != 0) {
                        switch (write_node) {
                            case 4:
                                switch (*((char *) lpOutBuffer + 3)) {
                                    case 1: // init
                                        FPGA_CMD_STATE = 65;
                                        break;
                                    case 2: // check
                                        FPGA_CMD_STATE = 66;
                                        break;
                                    case 3: // write
                                    {
                                        //int write_data = *((short *) lpOutBuffer + 4);
                                        break;
                                    }
                                    case 4: // write done
                                        FPGA_CMD_STATE = 67;
                                        break;
                                    default: // unknown
                                        return -1;
                                }
                                break;
                            case 5:
                                break;
                            case 9:
                                break;
                            case 64: // SRAM command
                                SRAM_CMD = *((char *) lpOutBuffer + 3);
                                switch (SRAM_CMD) {
                                    case 2: // read
                                        SRAM_PAGE = 0;
                                        break;
                                    case 3: // write
                                        SRAM_PAGE = 0;
                                        break;
                                    case 4: // done
                                        break;
                                    default: // unknown
                                        return -1;
                                }
                                break;
                            default: // unknown node
                                return -1;
                        }
                    }

                    // save node
                    FPGA_CUR_NODE = write_node;

                    // return out buffer size
                    return (int) nOutBufferSize;
                }

                // write 0x02
                if (*(DWORD *) lpInBuffer == 0x02) {

                    // check out buffer size
                    if (nOutBufferSize < 64)
                        return -1;

                    int cmd = *(char *) lpOutBuffer;
                    switch (cmd) {
                        case 0: // null write
                            break;
                        case 4: // unknown
                            break;
                        case 5: // 16seg
                        {
                            // write to status
                            IIDX_LED_TICKER_LOCK.lock();
                            if (!IIDXIO_LED_TICKER_READONLY)
                                memcpy(IIDXIO_LED_TICKER, (char *) lpOutBuffer + 2, 9);
                            IIDX_LED_TICKER_LOCK.unlock();

                            break;
                        }
                        case 64: // SRAM write
                        {
                            // get page
                            static char page = *((char *) lpOutBuffer + 1);
                            page = (char) SRAM_PAGE++;

                            // check page
                            if (page >= 12)
                                return -1;

                            // copy data
                            memcpy(SRAM_DATA + 62 * page, (uint8_t*) lpOutBuffer + 2, 62);

                            break;
                        }
                        default: // unknown node
                            return -1;
                    }

                    // return out buffer size
                    return (int) nOutBufferSize;
                }

                // unknown write
                return -1;

                // firmware upload
            case 0x22206D:

                // check buffer size
                if (nInBufferSize >= 2)
                    return (int) nOutBufferSize;
                else
                    return -1;

                // unknown control code
            default:
                log_warning("iidx", "EZUSB unknown: {}", dwIoControlCode);
                return -1;
        }
    } else
        return -1;
}

bool games::iidx::EZUSBHandle::close() {
    return true;
}
