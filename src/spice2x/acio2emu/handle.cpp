#include "handle.h"

#include "util/logging.h"
#include "util/utils.h" // ws2s

namespace acio2emu {
    class MasterNode : public Node {
    private:
        const IOBHandle *iob_;

    public:
        MasterNode(const IOBHandle *iob) : iob_(iob) { }

        bool handle_packet(const Packet &in, std::vector<uint8_t> &out) {
            // were we sent a command?
            if (in.payload.size() >= 2) {
                if (in.payload[0] != 0 || in.payload[1] != 1) {
                    // unknown command
                    return false;
                }

                // assign node ids
                out.push_back(0);
                out.push_back(1);
                for (int i = 0; i < iob_->number_of_nodes(); i++) {
                    out.push_back(i * 16);
                }
            }
        
            return true;
        }
    };

    IOBHandle::IOBHandle(std::wstring device) : device_(device) { 
        nodes_[0] = std::make_unique<MasterNode>(this); 
    }

    bool IOBHandle::register_node(std::unique_ptr<Node> node) {
        if ((number_of_nodes_ - 1) >= 16) {
            // too many nodes
            return false;
        }

        nodes_[number_of_nodes_++] = std::move(node);
        return true;
    }

    int IOBHandle::number_of_nodes() const {
        // don't include the master node
        return number_of_nodes_ - 1;
    }

    void IOBHandle::forward_packet_(const Packet &packet) {
        // clear the output queue
        output_ = {};

        auto node = packet.node / 2;
        if (node >= number_of_nodes_) {
            log_warning("acio2emu", "cannot forward packet: node out of range: {} >= {}", node, number_of_nodes_);
            return;
        }

        // forward the packet to the node
        std::vector<uint8_t> payload;
        if (!nodes_[node]->handle_packet(packet, payload)) {
            // error in handler
            return;
        }

        // encode the response
        encode_packet(output_, node, packet.tag, payload);
    }

    /*
     * CustomHandle
     */

    bool IOBHandle::open(LPCWSTR lpFileName) {
        if (device_ != lpFileName) {
            return false;
        }

        log_info("acio2emu", "Opened {} (ACIO2)", ws2s(device_));
        return true;
    }

    bool IOBHandle::close() {
        log_info("acio2emu", "Closed {} (ACIO2)", ws2s(device_));
        return true;
    }

    int IOBHandle::read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) {
        auto buffer = reinterpret_cast<uint8_t *>(lpBuffer);
        DWORD i = 0;

        while (!output_.empty() && i < nNumberOfBytesToRead) {
            buffer[i++] = output_.front();
            output_.pop();
        }

        return i;
    }

    int IOBHandle::write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) {
        auto buffer = reinterpret_cast<const uint8_t *>(lpBuffer);

        for (DWORD i = 0; i < nNumberOfBytesToWrite; i++) {
            if (decoder_.update(buffer[i])) {
                // forward the packet to a node
                forward_packet_(decoder_.packet());
            }
        }

        return nNumberOfBytesToWrite;
    }

    int IOBHandle::device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize) {
        return -1;
    }
}