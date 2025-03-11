#pragma once

#include <string>
#include <array>
#include <queue>
#include <memory> // std::unique_ptr
#include <cstdint>

#include "acio2emu/packet.h"
#include "acio2emu/node.h"

#include "hooks/devicehook.h"

namespace acio2emu {
    class IOBHandle : public CustomHandle {
    private:
        std::wstring device_;

        std::array<std::unique_ptr<Node>, 17> nodes_;
        // the first node is reserved for the "master" node
        int number_of_nodes_ = 1;

        PacketDecoder decoder_;
        std::queue<uint8_t> output_;

        void forward_packet_(const Packet &packet);

    public:
        IOBHandle(std::wstring device);

        bool register_node(std::unique_ptr<Node> node);
        int number_of_nodes() const;

        /*
         * CustomHandle
         */

        bool open(LPCWSTR lpFileName) override;
        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override;
        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override;
        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize) override;
        bool close() override;
    };
}