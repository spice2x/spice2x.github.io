#include "sate.h"
#include "util/logging.h"

using namespace acioemu;

games::ddr::DDRSATEHandle::SATEDevice::SATEDevice() {
    this->node_count = 7;
}

bool games::ddr::DDRSATEHandle::SATEDevice::parse_msg(
    acioemu::MessageData *msg_in,
    circular_buffer<uint8_t> *response_buffer
) {

    // check command
    switch (msg_in->cmd.code) {
        case ACIO_CMD_GET_VERSION: {

            // send version data
            auto msg = this->create_msg(msg_in, MSG_VERSION_SIZE);
            this->set_version(msg, 0x105, 0, 1, 1, 0, "DDRS");
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case ACIO_CMD_CLEAR:
        case ACIO_CMD_STARTUP:
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

bool games::ddr::DDRSATEHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM2") != 0) {
        return false;
    }

    log_info("ddr", "Opened COM2 (SATE)");

    // ACIO device
    acio_emu.add_device(new SATEDevice());

    return true;
}

int games::ddr::DDRSATEHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
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

int games::ddr::DDRSATEHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);

    // write to emu
    for (DWORD i = 0; i < nNumberOfBytesToWrite; i++) {
        acio_emu.write(buffer[i]);
    }

    // return all data written
    return (int) nNumberOfBytesToWrite;
}

int games::ddr::DDRSATEHandle::device_io(
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize
) {
    return -1;
}

bool games::ddr::DDRSATEHandle::close() {
    log_info("ddr", "Closed COM2 (SATE).");

    return true;
}
