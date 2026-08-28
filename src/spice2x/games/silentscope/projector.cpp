#include "projector.h"

#include <algorithm>
#include <cstring>
#include <cwchar>

#include "util/logging.h"

namespace {

    // request/response layout: identifier, command, two zero bytes, payload length,
    // the payload itself and a checksum over everything before it
    constexpr size_t HEADER_SIZE = 5;
    constexpr uint8_t RESPONSE_ID = 0x23;

    constexpr uint8_t COMMAND_COMMON_DATA = 0x8A;
    constexpr uint8_t COMMAND_TEMPERATURE = 0x99;
    constexpr uint8_t COMMAND_LAMP_CURRENT = 0x9B;

    // shown in the test menu as PROJTIM, PROJHEATIN and PROJHEAT
    constexpr uint32_t LAMP_SECONDS = 0;
    constexpr uint32_t TEMPERATURE_INTAKE = 25;
    constexpr uint32_t TEMPERATURE_EXHAUST = 35;

    // anything below 1000 is treated as a lamp anomaly and raises PROJERROR unless the exact
    // value was already recorded in /projecter/current on an earlier boot
    constexpr uint32_t LAMP_CURRENT = 1000;

    void put32(uint8_t *dest, uint32_t value) {
        dest[0] = (uint8_t) (value & 0xFF);
        dest[1] = (uint8_t) ((value >> 8) & 0xFF);
        dest[2] = (uint8_t) ((value >> 16) & 0xFF);
        dest[3] = (uint8_t) ((value >> 24) & 0xFF);
    }
}

bool games::silentscope::ProjectorHandle::open(LPCWSTR lpFileName) {
    if (wcscmp(lpFileName, L"COM2") != 0 && wcscmp(lpFileName, L"\\\\.\\COM2") != 0) {
        return false;
    }

    log_info("silentscope", "Opened COM2 (projector)");

    return true;
}

int games::silentscope::ProjectorHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
    std::lock_guard<std::mutex> lock(this->mutex);

    auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);

    DWORD bytes_read = 0;
    while (bytes_read < nNumberOfBytesToRead && !this->response.empty()) {
        buffer[bytes_read++] = this->response.front();
        this->response.pop_front();
    }

    return (int) bytes_read;
}

int games::silentscope::ProjectorHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
    std::lock_guard<std::mutex> lock(this->mutex);

    auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);
    this->request.insert(this->request.end(), buffer, buffer + nNumberOfBytesToWrite);

    while (this->request.size() >= HEADER_SIZE) {
        const size_t packet_size = HEADER_SIZE + this->request[4] + 1;
        if (this->request.size() < packet_size) {
            break;
        }

        this->process_request(this->request.data());
        this->request.erase(this->request.begin(), this->request.begin() + packet_size);
    }

    return (int) nNumberOfBytesToWrite;
}

size_t games::silentscope::ProjectorHandle::bytes_available() {
    std::lock_guard<std::mutex> lock(this->mutex);

    return this->response.size();
}

bool games::silentscope::ProjectorHandle::close() {
    std::lock_guard<std::mutex> lock(this->mutex);

    this->request.clear();
    this->response.clear();

    log_info("silentscope", "Closed COM2 (projector)");

    return true;
}

void games::silentscope::ProjectorHandle::process_request(const uint8_t *packet) {
    const uint8_t command = packet[1];
    const uint8_t length = packet[4];
    const uint8_t *data = packet + HEADER_SIZE;

    switch (command) {
        case COMMAND_COMMON_DATA: {

            // the game only looks at the lamp usage time near the end of the block
            std::vector<uint8_t> payload(98, 0);
            put32(&payload[94], LAMP_SECONDS);
            this->reply(command, payload);
            break;
        }
        case COMMAND_TEMPERATURE: {

            // the requested sensor is echoed back along with its reading
            const uint8_t sensor = length > 0 ? data[0] : 0;
            std::vector<uint8_t> payload(5, 0);
            payload[0] = sensor;
            put32(&payload[1], sensor == 0 ? TEMPERATURE_INTAKE : TEMPERATURE_EXHAUST);
            this->reply(command, payload);
            break;
        }
        case COMMAND_LAMP_CURRENT: {

            // the three byte item selector is echoed back along with the measurement
            std::vector<uint8_t> payload(7, 0);
            memcpy(payload.data(), data, std::min<size_t>(length, 3));
            put32(&payload[3], LAMP_CURRENT);
            this->reply(command, payload);
            break;
        }
        default:
            log_misc("silentscope", "unknown projector command {:#04x}", command);
            this->reply(command, {});
            break;
    }
}

void games::silentscope::ProjectorHandle::reply(uint8_t command, const std::vector<uint8_t> &data) {
    std::vector<uint8_t> packet {
        RESPONSE_ID, command, 0x00, 0x00, (uint8_t) data.size()
    };
    packet.insert(packet.end(), data.begin(), data.end());

    uint8_t checksum = 0;
    for (auto byte : packet) {
        checksum += byte;
    }
    packet.push_back(checksum);

    this->response.insert(this->response.end(), packet.begin(), packet.end());
}
