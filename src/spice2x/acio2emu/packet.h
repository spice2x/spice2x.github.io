#pragma once

#include <vector>
#include <queue>
#include <random> // std::linear_congruential_engine
#include <cstdint>

#include "acio2emu/internal/lz.h"

namespace acio2emu {
    struct Packet {
        uint8_t node;
        uint8_t tag;
        std::vector<uint8_t> payload;
    };

    class PacketDecoder {
    private:
        Packet packet_ = {};

        // order matters, don't change this enum!
        enum payloadEncoding {
            byteStuffing,
            raw,
            unknown,
            replace,
            lz,
        } encoding_;

        uint32_t payload_size_ = 0, payload_size_count_ = 0;

        // payloadEncoding::replace state
        uint8_t substitute_; 

        // payloadEncoding::lz state
        detail::InflateTransformer inflate_;

        // deobfuscation state
        bool obfuscated_;
        std::linear_congruential_engine<uint32_t, 1103515245, 12345, 0> lcg_; 

        enum class readStep {
            idle,
            readNode,
            readTag,
            readPayloadSize,
            readPayloadFlags,
            readReplacementByte,
            readPayload,
            readEscaped,
        } step_ = readStep::idle;

        void set_step_(readStep s);
        void reset_(readStep s);

        int update_payload_size_(uint8_t b);
        uint8_t deobfuscate_(uint8_t b);

    public:
        bool update(uint8_t b);
        const Packet &packet();
    };

    bool encode_packet(std::queue<uint8_t> &out, uint8_t node, uint8_t tag, const std::vector<uint8_t> &payload);
}