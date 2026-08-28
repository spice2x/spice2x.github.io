#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "hooks/devicehook.h"

namespace games::silentscope {

    // The cabinet talks to its projector over COM2. Without an answer the game stops at
    // I/O error 5-1560-0004 (IOCOM2, "the projector is not connected correctly").
    class ProjectorHandle : public CustomHandle {
    public:
        bool open(LPCWSTR lpFileName) override;
        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
        size_t bytes_available() override;
        bool close() override;

    private:
        std::mutex mutex;
        std::vector<uint8_t> request;
        std::deque<uint8_t> response;

        void process_request(const uint8_t *packet);
        void reply(uint8_t command, const std::vector<uint8_t> &data);
    };
}
