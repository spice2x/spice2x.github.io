#include "usbmem.h"
#include "util/logging.h"

void games::ddr::DDRUSBMEMHandle::respond(const char *data) {
    size_t len = strlen(data) + 1;
    memcpy(response_data, data, len);
    memcpy(response_data + len, "\r>", 2);
    response_data_size = len + 2;
}

bool games::ddr::DDRUSBMEMHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM3") != 0) {
        return false;
    }
    log_info("ddr", "Opened COM3 (USBMEM)");
    return true;
}

int games::ddr::DDRUSBMEMHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

    // check for response
    if (response_data_size && nNumberOfBytesToRead >= response_data_size) {
        size_t bytes_read = response_data_size;
        memcpy(lpBuffer, response_data, response_data_size);
        response_data_size = 0;
        return (int) bytes_read;
    }

    // no data
    return 0;
}

int games::ddr::DDRUSBMEMHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {

    // check CMD
    if (!memcmp(lpBuffer, "sver", 4))
        respond("done GQHDXJAA SPICE");
    else if (!memcmp(lpBuffer, "on_a", 4) ||
             !memcmp(lpBuffer, "on_b", 4) ||
             !memcmp(lpBuffer, "offa", 4) ||
             !memcmp(lpBuffer, "offb", 4) ||
             !memcmp(lpBuffer, "lma ", 4) ||
             !memcmp(lpBuffer, "lmb ", 4))
        respond("done");
    else
        respond("not connected");

    // return all data written
    return (int) nNumberOfBytesToWrite;
}

int games::ddr::DDRUSBMEMHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                           LPVOID lpOutBuffer, DWORD nOutBufferSize) {
    return -1;
}

bool games::ddr::DDRUSBMEMHandle::close() {
    log_info("ddr", "Closed COM3 (USBMEM).");
    return true;
}
