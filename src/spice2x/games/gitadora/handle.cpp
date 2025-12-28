#include "handle.h"
#include "misc/eamuse.h"
#include "rawinput/rawinput.h"
#include "util/utils.h"
#include "util/time.h"
#include "io.h"
#include "gitadora.h"
#include "j32d.h"
#include "j33i.h"
#include "device.h"


using namespace acioemu;
static games::gitadora::GitadoraDevice *device;

bool games::gitadora::GitaDoraSerialHandle::open(LPCWSTR lpFileName)
{
    if (wcscmp(lpFileName, L"COM1") != 0) {
        return false;
    }

    bool is_drum = games::gitadora::is_drum();

    if (is_drum) {
        device = new J32DSerialDevice();
    }
    else {
        device = new J33ISerialDevice();
    }

    acio_emu.add_device(device);
    log_info("gitadora", "Opened COM1 ({} DEVICE)", is_drum ? "J32D" : "J33I");
    return true;
}

int games::gitadora::GitaDoraSerialHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead)
{

    auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);

    // the game does not send cmd 0x12F to the IO
    // but it is expected to receive 0x12F for user input
    // so i manually created a payload so that parse_msg can be used
    // to write to the response_buffer
    if (device->is_ready) {
        uint8_t chk = 0;
        MessageData* msg = ACIODeviceEmu::create_msg(0x1, 0x12F, 0, 0);
        msg->addr = 0x1;
        auto write_buffer = reinterpret_cast<uint8_t *>(msg);
        while (acio_emu.read() != std::nullopt); // there may be old stuffs in the buffer
        acio_emu.write(0xAA);
        acio_emu.write(0xAA);

        for(DWORD i = 0; i < 5; i++) {
            chk += write_buffer[i];
            acio_emu.write(write_buffer[i]);
        }
        // direct copy and paste from write_msg
        if (chk == ACIO_SOF || chk == ACIO_ESCAPE) {
            acio_emu.write(ACIO_ESCAPE);
            acio_emu.write(~chk);
        } else {
            acio_emu.write(chk);
        }
    }

    // read from emu
    DWORD bytes_read = 0;
    while (bytes_read < nNumberOfBytesToRead)
    {
        auto cur_byte = acio_emu.read();

        if (cur_byte.has_value()) {
            buffer[bytes_read++] = cur_byte.value();
        }
        else {
            break;
        }
    }

#ifdef ACIOEMU_LOG
    log_info("gitadora", "Read IO Data: {}", bin2hex(buffer, bytes_read));
#endif
    // return amount of bytes read
    return (int)bytes_read;
}


int games::gitadora::GitaDoraSerialHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite)
{
    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);

    // write to emu
    for (DWORD i = 0; i < nNumberOfBytesToWrite; i++) {
        acio_emu.write(buffer[i]);
    }

#ifdef ACIOEMU_LOG
    log_info("gitadora", "Write IO Data: {}", bin2hex(buffer, nNumberOfBytesToWrite));
#endif
    // return all data written
    return (int)nNumberOfBytesToWrite;
}

int games::gitadora::GitaDoraSerialHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                                                    LPVOID lpOutBuffer, DWORD nOutBufferSize) {
 
    return -1;
}

bool games::gitadora::GitaDoraSerialHandle::close() {
    
    log_info("gitadora", "Closed COM1");
    return true;
}
