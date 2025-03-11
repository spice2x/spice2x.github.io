#include "bi2x.h"

namespace acio2emu::firmware {
    bool BI2XNode::handle_packet(const acio2emu::Packet &in, std::vector<uint8_t> &out) {
        auto cur = in.payload.begin();
        while ((cur + 1) < in.payload.end()) {
            auto cmd = (cur[0] << 8) | cur[1];
            out.push_back(*cur++);
            out.push_back(*cur++);
            out.push_back(0);

            switch (cmd) {
            case 2: // query firmware version
                read_firmware_version(out);
                cur = in.payload.end();
                break;
            
            case 16:
                out.push_back(2);
                cur = in.payload.end();
                break;

            case 800:
            case 802:
            case 19:
                cur = in.payload.end();
                break;

            case 120:
                out.push_back(3);
                cur = in.payload.end();
                break;

            case 801:
                out.push_back(33);
                out.push_back(0);
                cur = in.payload.end();
                break;

            case 784: // poll input
                if (!read_input(out)) {
                    return false;
                }
                break;

            case 785: { // write output
                auto count = write_output(std::span{&*cur, static_cast<size_t>(in.payload.end() - cur)});
                if (count < 0) {
                    return false;
                }
                cur += count;
                break;
            }

            case 786:
                cur += 4;
                break;

            default:
                log_warning("bi2x", "unknown command: {}", cmd);
                return false;
            }
        } 

        return true;
    }
}