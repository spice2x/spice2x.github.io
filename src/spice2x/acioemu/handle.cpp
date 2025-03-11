#include "handle.h"

#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"

acioemu::ACIOHandle::ACIOHandle(LPCWSTR lpCOMPort) {
    this->com_port = lpCOMPort;
}

bool acioemu::ACIOHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, com_port) != 0) {
        return false;
    }

    log_info("acioemu", "Opened {} (ACIO)", ws2s(com_port));

    // ACIO device
    acio_emu.add_device(new acioemu::ICCADevice(false, true, 2));

    return true;
}

int acioemu::ACIOHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
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

int acioemu::ACIOHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);

    // write to emu
    for (DWORD i = 0; i < nNumberOfBytesToWrite; i++) {
        acio_emu.write(buffer[i]);
    }

    // return all data written
    return (int) nNumberOfBytesToWrite;
}

int acioemu::ACIOHandle::device_io(
        DWORD dwIoControlCode,
        LPVOID lpInBuffer,
        DWORD nInBufferSize,
        LPVOID lpOutBuffer,
        DWORD nOutBufferSize
) {
    return -1;
}

size_t acioemu::ACIOHandle::bytes_available() {
    return acio_emu.bytes_available();
}

bool acioemu::ACIOHandle::close() {
    log_info("acioemu", "Closed {} (ACIO)", ws2s(com_port));

    return true;
}
