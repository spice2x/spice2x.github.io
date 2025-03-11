#include "gunio.h"
#include "util/logging.h"

bool games::mga::SpiceGearGunHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM1") != 0) {
        return false;
    }
    log_info("metalgear", "Opened gun device on COM1");
    return true;
}

int games::mga::SpiceGearGunHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

    // minimum buffer size
    if ((command == 0u) || nNumberOfBytesToRead < 2)
        return 0;

    // execute command
    int bytes_read = 0;
    switch (command) {
        case 0x04: // get version

            // write version
            *((unsigned char *) lpBuffer + bytes_read++) = version;

            break;
        case 0x08: // get input state
        {
            // check buffer size
            if (nNumberOfBytesToRead < 12)
                return 0;

            // get cursor position
            POINT cursor_pos{};
            GetCursorPos(&cursor_pos);

            // get screen size
            RECT screen_size{};
            GetWindowRect(GetDesktopWindow(), &screen_size);
            auto screen_width = (unsigned short) (screen_size.right - screen_size.left);
            auto screen_height = (unsigned short) (screen_size.bottom - screen_size.top);

            // calculate cursor position
            p1_x = (unsigned short) (((cursor_pos.x * 1024) / screen_width) % 1024);
            p1_y = (unsigned short) (1024 - (((cursor_pos.y * 1024) / screen_height) % 1024));

            // gun P1
            *((unsigned char *) lpBuffer + bytes_read++) = 0x00;
            *((unsigned char *) lpBuffer + bytes_read++) = HIBYTE(p1_x);
            *((unsigned char *) lpBuffer + bytes_read++) = LOBYTE(p1_x);
            *((unsigned char *) lpBuffer + bytes_read++) = HIBYTE(p1_y);
            *((unsigned char *) lpBuffer + bytes_read++) = LOBYTE(p1_y);
            bytes_read += 3;

            // fill buffer
            *((unsigned char *) lpBuffer + bytes_read++) = 0x75;
            *((unsigned char *) lpBuffer + bytes_read++) = 0x75;
            *((unsigned char *) lpBuffer + bytes_read++) = 0x75;

            break;
        }
        case 0x0C: // not called in game - empty

            // empty data
            *((unsigned char *) lpBuffer + bytes_read++) = 0x00;

            break;
        case 0x10: // get DIP switches

            // clear bits
            *((unsigned char *) lpBuffer + bytes_read) = 0x00;

            // set bits
            if (frequency == 1)
                *((unsigned char *) lpBuffer + bytes_read) |= 0x80;
            if (frequency == 2)
                *((unsigned char *) lpBuffer + bytes_read) |= 0x40;
            if (frequency == 3)
                *((unsigned char *) lpBuffer + bytes_read) |= 0x20;
            if (frequency == 4)
                *((unsigned char *) lpBuffer + bytes_read) |= 0x10;

            break;
        case 0x14: // frequency 1

            // set frequency and return empty data
            frequency = 1;
            *((unsigned char *) lpBuffer + bytes_read++) = 0x00;

            break;
        case 0x18: // frequency 2

            // set frequency and return empty data
            frequency = 2;
            *((unsigned char *) lpBuffer + bytes_read++) = 0x00;

            break;
        case 0x19: // frequency 3

            // set frequency and return empty data
            frequency = 3;
            *((unsigned char *) lpBuffer + bytes_read++) = 0x00;

            break;
        case 0x1A: // frequency 4

            // set frequency and return empty data
            frequency = 4;
            *((unsigned char *) lpBuffer + bytes_read++) = 0x00;

            break;
        case 0x1C: // led board check

            // check buffer size
            if (nNumberOfBytesToRead < 11)
                return 0;

            // set all bits
            memset(lpBuffer, 0xFF, 10);
            bytes_read += 10;

            break;
        default:
            log_warning("metalgear", "unknown opcode: 0x{:02X}", command);
            *((unsigned char *) lpBuffer + bytes_read++) = 0x01;
    }

    // reset command
    command = 0;

    // calculate checksum
    unsigned char checksum = 0xFF;
    for (int i = 0; i < bytes_read; i++)
        checksum -= *((unsigned char *) lpBuffer + i);
    *((unsigned char *) lpBuffer + bytes_read++) = checksum;

    // return amount of bytes read
    return bytes_read;
}

int games::mga::SpiceGearGunHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {

    // save command
    if (nNumberOfBytesToWrite > 0)
        command = *((unsigned char *) lpBuffer);

    return (int) nNumberOfBytesToWrite;
}

int games::mga::SpiceGearGunHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                              LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::mga::SpiceGearGunHandle::close() {
    log_info("metalgear", "Closed gun device on COM1");
    return true;
}
