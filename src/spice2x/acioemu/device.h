#pragma once

#include <string>

#include "util/circular_buffer.h"

// convert big-endian to little-endian
#define acio_u16 _byteswap_ushort
#define acio_u32 _byteswap_ulong

namespace acioemu {

    constexpr uint8_t ACIO_SOF = 0xAA;
    constexpr uint8_t ACIO_ESCAPE = 0xFF;
    constexpr uint8_t ACIO_BROADCAST = 0x70;
    constexpr uint8_t ACIO_RESPONSE_FLAG = 0x80;

    // general command codes
    enum acio_cmd_codes {
        ACIO_CMD_ASSIGN_ADDRS = 0x0001,
        ACIO_CMD_GET_VERSION  = 0x0002,
        ACIO_CMD_STARTUP      = 0x0003,
        ACIO_CMD_KEEPALIVE    = 0x0080,
        ACIO_CMD_CLEAR        = 0x0100,
    };

    // message structs
#pragma pack(push, 1)
    struct VersionData {
        uint32_t type;
        uint8_t flag;
        uint8_t ver_major;
        uint8_t ver_minor;
        uint8_t ver_rev;
        char code[4];
        char date[16];
        char time[16];
    };

    struct MessageData {
        uint8_t addr;

        union {
            struct {
                uint16_t code;
                uint8_t pid;
                uint8_t data_size;

                union {
                    uint8_t raw[0xFF];
                    uint8_t status;
                    VersionData data_version;
                };
            } cmd;

            struct {
                uint8_t data_size;
                uint8_t raw[0xFF];
            } broadcast;
        };
    };
#pragma pack(pop)

    // message sizes
    constexpr size_t MSG_HEADER_SIZE = 5;
    constexpr size_t MSG_VERSION_SIZE = sizeof(VersionData);

    class ACIODeviceEmu {
    public:

        // attributes
        uint8_t node_count = 0;

        /*
         * Helper functions for getting/setting the message contents
         */
        static void set_header(MessageData* data, uint8_t addr, uint16_t code, uint8_t pid, uint8_t data_size);
        static void set_version(MessageData* data, uint32_t type, uint8_t flag,
                         uint8_t ver_major, uint8_t ver_minor, uint8_t ver_rev,
                         std::string code);

        /*
         * This function creates a basic message with optional parameter data.
         * If data is set to null, the parameter data will be initialized with 0x00
         */
        static MessageData* create_msg(uint8_t addr, uint16_t cmd, uint8_t pid,
                size_t data_size, uint8_t *data = nullptr);
        static MessageData* create_msg(MessageData* msg_in, size_t data_size, uint8_t *data = nullptr);

        /*
         * Helper functions for generating messages
         */
        static MessageData* create_msg_status(uint8_t addr, uint16_t code, uint8_t pid, uint8_t status);
        static MessageData* create_msg_status(MessageData* msg_in, uint8_t status);

        virtual ~ACIODeviceEmu() = default;

        virtual bool is_applicable(uint8_t node_offset, uint8_t node);
        virtual bool parse_msg(MessageData *msg_in, circular_buffer<uint8_t> *response_buffer) = 0;
        static void write_msg(const uint8_t *data, size_t size, circular_buffer<uint8_t> *response_buffer);
        static void write_msg(MessageData *msg, circular_buffer<uint8_t> *response_buffer);
    };
}
