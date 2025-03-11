#pragma once

#include <vector>

#include "acio2emu/packet.h"

namespace acio2emu {
    class Node {
    public:
        virtual ~Node() {}

        virtual bool handle_packet(const Packet &in, std::vector<uint8_t> &out) = 0;
    };
}