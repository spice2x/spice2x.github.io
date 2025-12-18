#include "packet.h"

#include "util/logging.h"

#include "acio2emu/internal/crc.h"

namespace acio2emu {
    static constexpr uint8_t SOF = 0xAA;
    static constexpr uint8_t ESC = 0xFF;

    static void encode_payload_(std::queue<uint8_t> &out, const std::vector<uint8_t> &payload) {
        for (auto b : payload) {
            if (b == SOF || b == ESC) {
                out.push(ESC);
                b = ~b;
            }
            out.push(b); 
        }
        // compute and write the payload's CRC
        out.push(detail::crc7_lgp_48(0x7F, payload.data(), payload.size()) ^ 0x7F);
    }

    bool encode_packet(std::queue<uint8_t> &out, uint8_t node, uint8_t tag, const std::vector<uint8_t> &payload) {
        auto size = payload.size();
        if (size > 127) {
            log_warning("acio2emu", "cannot encode packet: payload too large: {} > 127", payload.size());
            return false;
        }

        // build the header
        uint8_t header[5] = {
            SOF,
            static_cast<uint8_t>(node * 3),
            tag,
            static_cast<uint8_t>(size),
            0,
        };
        // compute the header's CRC
        header[4] = detail::crc4_lgp_c(0x0F, &header[1], sizeof(header) - 1) ^ 0x0F;
        // push the header to the output queue
        for (size_t i = 0; i < sizeof(header); i++) {
            out.push(header[i]);
        }

        encode_payload_(out, payload);
        return true;
    }

    void PacketDecoder::set_step_(readStep s) {
#ifndef NDEBUG
    auto valid = true;
    switch (s) {
        case readStep::idle:
        case readStep::readNode:
            // transition from any step/state allowed
            break;

        case readStep::readTag:
            if (step_ != readStep::readNode) {
                valid = false;
            }
            break;

        case readStep::readPayloadSize:
            if (step_ != readStep::readTag) {
                valid = false;
            }
            break;

        case readStep::readPayloadFlags:
            if (step_ != readStep::readPayloadSize) {
                valid = false;
            }
            break;

        case readStep::readReplacementByte:
            if (step_ != readStep::readPayloadFlags) {
                valid = false;
            }
            break;

        case readStep::readPayload:
            if (step_ != readStep::readPayloadFlags && 
                step_ != readStep::readReplacementByte &&
                step_ != readStep::readEscaped
            ) {
                valid = false;
            }
            break;

        case readStep::readEscaped:
            if (step_ != readStep::readPayload) {
                valid = false;
            }
            break;

        default:
            log_fatal(
                "acio2emu",
                "cannot set step: unknown value: {}",
                static_cast<uint32_t>(s));
            break;
        }

        if (!valid) {
            log_fatal(
                "acio2emu",
                "illegal transition detected: {} -> {}",
                static_cast<uint32_t>(step_),
                static_cast<uint32_t>(s));
        }
#endif
        step_ = s;
    }

    int PacketDecoder::update_payload_size_(uint8_t b) {
        if ((b & 0x80) == 0) {
            payload_size_ = (payload_size_ << 7) | (b & 0x7F);
            // finished
            return 0;
        }
        else if ((b & 0x40) != 0 && payload_size_count_ < 5) {
            payload_size_count_++;
            payload_size_count_ = (payload_size_count_ << 6) | (b & 0x3F);
            // continuation required
            return 1;
        }
        else {
            // invalid value or invalid state
            return -1;
        }
    }

    uint8_t PacketDecoder::deobfuscate_(uint8_t b) {
        if ((b ^ 0xAA) == 0) {
            return b;
        }

        auto mask = 0x55;
        if ((b & 0x80) == 0) {
            mask = 0x7F;
        }

        return (b ^ lcg_()) & mask;
    }

    void PacketDecoder::reset_(readStep s) {
        set_step_(s);
        packet_ = {};
        payload_size_ = 0;
        payload_size_count_ = 0;
    }

    bool PacketDecoder::update(uint8_t b) {
        // is this the start of a packet?
        if (b == SOF) {
            reset_(readStep::readNode);
            return false;
        }

        switch (step_) {
        case readStep::readNode:
            packet_.node = b;

            set_step_(readStep::readTag);
            break;

        case readStep::readTag:
            packet_.tag = b;

            set_step_(readStep::readPayloadSize);
            break;

        case readStep::readPayloadSize: {
            auto status = update_payload_size_(b);
            if (status == 0) {
                // finished reading payload size
                packet_.payload.reserve(payload_size_);
                set_step_(readStep::readPayloadFlags);
            }  
            else if (status == -1) {
                // reset on error
                reset_(readStep::idle);
            }
            break;
        }

        case readStep::readPayloadFlags:
            obfuscated_ = (b & (1 << 4)) != 0;
            encoding_ = static_cast<payloadEncoding>(b >> 5);
        
            if (obfuscated_) {
                lcg_.seed(packet_.tag ^ 0x55);
            }

            if (encoding_ == payloadEncoding::replace) {
                set_step_(readStep::readReplacementByte);
            }
            else {
                set_step_(readStep::readPayload);

                if (encoding_ == payloadEncoding::lz) {
                    // reset the InflateTransformer
                    inflate_ = {};
                }
            }
            break;

        case readStep::readReplacementByte:
            substitute_ = b;

            set_step_(readStep::readPayload);
            break;

        case readStep::readPayload:
            // do we need to deobfuscate?
            if (obfuscated_) {
                b = deobfuscate_(b);
            }

            if (encoding_ == payloadEncoding::lz) {
                inflate_.put(b);
                for (int i = inflate_.get(); i >= 0; i = inflate_.get()) {
                    packet_.payload.push_back(i);
                }
            }
            else if (encoding_ == payloadEncoding::replace && b == substitute_) {
                packet_.payload.push_back(SOF);
            }
            else if (encoding_ == payloadEncoding::byteStuffing && b == ESC) {
                set_step_(readStep::readEscaped);
                break;
            }
            else {
                packet_.payload.push_back(b);
            }
            break;

        case readStep::readEscaped:
            b = ~b;
            if (obfuscated_) {
                b = deobfuscate_(b);
            }
            packet_.payload.push_back(b);

            set_step_(readStep::readPayload);
            break;

        default:
            break;
        }

        if ((step_ == readStep::readPayload || step_ == readStep::readPayloadFlags) && 
            (packet_.payload.size() >= payload_size_)) {
            set_step_(readStep::idle);
            // finished reading packet
            return true;
        }

        return false;
    }

    const Packet &PacketDecoder::packet() {
        return packet_;
    }
}