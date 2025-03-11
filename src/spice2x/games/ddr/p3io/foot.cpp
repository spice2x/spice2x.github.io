#include "foot.h"

#include "rawinput/rawinput.h"
#include "util/logging.h"

#include "../ddr.h"
#include "../io.h"

bool games::ddr::DDRFOOTHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM1") != 0) {
        return false;
    }
    log_info("ddr", "Opened COM1 (FOOT)");
    return true;
}

int games::ddr::DDRFOOTHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {

    // check data trigger and buffer size
    if (data_trigger && nNumberOfBytesToRead >= 1) {
        data_trigger = false;
        memset(lpBuffer, 0x11, 1);
        return 1;
    }

    // no data
    return 0;
}

int games::ddr::DDRFOOTHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {

    // check buffer size for lights
    if (nNumberOfBytesToWrite == 4) {

        // mappings
        static const size_t mapping_bits[] = {
                4, 6, 3, 5,     // P1 L, U, R, D,
                12, 14, 11, 13, // P2 L, U, R, D
                22              // NEON
        };

        static const size_t mapping[] = {
                Lights::P1_FOOT_LEFT,
                Lights::P1_FOOT_UP,
                Lights::P1_FOOT_RIGHT,
                Lights::P1_FOOT_DOWN,
                Lights::P2_FOOT_LEFT,
                Lights::P2_FOOT_UP,
                Lights::P2_FOOT_RIGHT,
                Lights::P2_FOOT_DOWN,
                Lights::NEON
        };

        // get light bits
        uint32_t light_bits = *((uint32_t*) lpBuffer);

        // get lights
        auto &lights = get_lights();

        // bit scan
        for (size_t i = 0; i < 8; i++) {
            float value = (light_bits & (1 << mapping_bits[i])) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[i]), value);
        }

        // only set the neon if in SD mode
        // p3io sets it for HD mode.
        if (games::ddr::SDMODE) {
            float value = (light_bits & (1 << mapping_bits[8])) ? 1.f : 0.f;
            GameAPI::Lights::writeLight(RI_MGR, lights.at(mapping[8]), value);
        }

        // flush
        RI_MGR->devices_flush_output();
    }

    // trigger out data
    data_trigger = true;

    // return all data written
    return (int) nNumberOfBytesToWrite;
}

int games::ddr::DDRFOOTHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                                     DWORD nOutBufferSize) {
    return -1;
}

bool games::ddr::DDRFOOTHandle::close() {
    log_info("ddr", "Closed COM1 (FOOT).");
    return true;
}
