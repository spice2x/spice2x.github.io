#pragma once

#include <cstdint>
#include <vector>

#include "util/circular_buffer.h"

#include "device.h"
#include "icca.h"

namespace acioemu {

    class ACIOEmu {
    private:
        std::vector<ACIODeviceEmu *> *devices;
        circular_buffer<uint8_t> *response_buffer;
        circular_buffer<uint8_t> *read_buffer;
        bool invert = false;

        void msg_parse();

    public:

        explicit ACIOEmu();
        ~ACIOEmu();

        void add_device(ACIODeviceEmu *device);

        void write(uint8_t byte);
        std::optional<uint8_t> read();
        size_t bytes_available();
    };
}
