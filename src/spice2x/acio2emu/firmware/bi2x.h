#pragma once

#include <vector>
#include <span>
#include <cstdint>

#include "acio2emu/node.h"
#include "util/logging.h"

namespace acio2emu::firmware {
    class BI2XNode : public Node {
        virtual void read_firmware_version(std::vector<uint8_t> &buffer) = 0;

        virtual bool read_input(std::vector<uint8_t> &buffer) = 0;
        virtual int write_output(std::span<const uint8_t> buffer) = 0;

        /*
         * acio2emu::Node
         */
        bool handle_packet(const acio2emu::Packet &in, std::vector<uint8_t> &out) override;
    };
}