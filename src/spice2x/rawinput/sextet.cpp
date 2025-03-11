#include "sextet.h"
#include <cstring>
#include <stdint.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include "util/logging.h"

using namespace std;


const string rawinput::SextetDevice::LIGHT_NAMES[LIGHT_COUNT] = {
    "Neon",

    "P1 Halogen Upper",
    "P1 Halogen Lower",
    "P2 Halogen Upper",
    "P2 Halogen Lower",

    "P1 Button",
    "P2 Button",

    "P1 Foot Up",
    "P1 Foot Down",
    "P1 Foot Left",
    "P1 Foot Right",

    "P2 Foot Up",
    "P2 Foot Down",
    "P2 Foot Left",
    "P2 Foot Right"
};

inline void rawinput::SextetDevice::fill_packet(uint8_t *buffer, bool *light_state) {

    /*
     * Refer to this doucment for more info
     * https://github.com/stepmania/stepmania/blob/master/src/arch/Lights/LightsDriver_SextetStream.md#bit-meanings
     */

    buffer[0] = 0xC0 | (uint8_t) (
        (light_state[SextetLights::BASS_NEON] << 5) |
        (light_state[SextetLights::BASS_NEON] << 4) |
        (light_state[SextetLights::MARQUEE_P2_LOWER] << 3) |
        (light_state[SextetLights::MARQUEE_P1_LOWER] << 2) |
        (light_state[SextetLights::MARQUEE_P2_UPPER] << 1) |
        (light_state[SextetLights::MARQUEE_P1_UPPER] << 0)
    );

    buffer[1] = 0xC0 | (uint8_t) (
        (light_state[SextetLights::P1_MENU] << 5) |
        (light_state[SextetLights::P1_MENU] << 4)
    );

    buffer[2] = 0xC0;

    buffer[3] = 0xC0 | (uint8_t) (
        (light_state[SextetLights::P1_DOWN] << 3) |
        (light_state[SextetLights::P1_UP] << 2) |
        (light_state[SextetLights::P1_RIGHT] << 1) |
        (light_state[SextetLights::P1_LEFT] << 0)
    );

    buffer[4] = 0xC0;
    buffer[5] = 0xC0;
    buffer[6] = 0xC0;

    buffer[7] = 0xC0 | (uint8_t) (
        (light_state[SextetLights::P2_MENU] << 5) |
        (light_state[SextetLights::P2_MENU] << 4)
    );

    buffer[8] = 0xC0;

    buffer[9] = 0xC0 | (uint8_t) (
        (light_state[SextetLights::P2_DOWN] << 3) |
        (light_state[SextetLights::P2_UP] << 2) |
        (light_state[SextetLights::P2_RIGHT] << 1) |
        (light_state[SextetLights::P2_LEFT] << 0)
    );

    buffer[10] = 0xC0;
    buffer[11] = 0xC0;
    buffer[12] = 0xC0;

    // terminate with LF
    buffer[13] = '\n';
}

rawinput::SextetDevice::SextetDevice(std::string device_name) {
    this->device_name = device_name;
    is_connected = false;
    all_off();
}

rawinput::SextetDevice::~SextetDevice() {
    disconnect();
}

void rawinput::SextetDevice::set_all(bool state) {
    for (int i = 0; i < LIGHT_COUNT; i++)
        light_state[i] = state;
}

void rawinput::SextetDevice::all_on() {
    set_all(true);
}

void rawinput::SextetDevice::all_off() {
    set_all(false);
}

bool rawinput::SextetDevice::connect() {

    // open device
    device = CreateFileA(
            this->device_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            0,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            0
    );

    // check device
    if (device == INVALID_HANDLE_VALUE)
        return false;

    // get comm state
    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(device, &dcb)) {
        log_warning("sextet", "GetCommState failed: 0x{:08x}", GetLastError());
        return false;
    }

    // set comm state
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(device, &dcb)) {
        log_warning("sextet", "SetCommState failed: 0x{:08x}", GetLastError());
        return false;
    }

    // success
    is_connected = true;
    return true;
}

bool rawinput::SextetDevice::disconnect() {
    return CloseHandle(device);
}

bool rawinput::SextetDevice::push_light_state() {

    // build packet
    uint8_t buffer[FULL_SEXTET_COUNT];
    this->fill_packet(&(buffer[0]), &(light_state[0]));

    // write data to device
    DWORD bytes_written;
    WriteFile(
            device,
            buffer,
            FULL_SEXTET_COUNT,
            &bytes_written,
            NULL
    );

    // verify written bytes count
    return bytes_written == FULL_SEXTET_COUNT;
}
