#pragma once

#include <string>

#include <windows.h>

#include "crypt.h"
#include "message.h"

enum reader_cmd {
    READER_CMD_SET_ID         = 0x01,
    READER_CMD_VERSION        = 0x02,
    READER_CMD_INIT2          = 0x03,
    READER_CMD_REINITIALIZE   = 0x30,
    READER_CMD_READ_CARD_UID  = 0x31,
    READER_CMD_GET_STATUS     = 0x34,
    READER_CMD_SET_ACTION     = 0x35,
    READER_CMD_SLEEP_MODE     = 0x3A,
    READER_CMD_KEY_EXCHANGE   = 0x60,
    READER_CMD_RFID_READ_UID  = 0x61,
    READER_CMD_GET_STATUS_ENC = 0x64,
};

enum reader_action {
    READER_ACTION_BLOCK       = 0x00,
    READER_ACTION_ACCEPT_CARD = 0x11,
    READER_ACTION_EJECT_CARD  = 0x12,
};

class Reader {

public:
    uint8_t keypad_started = 0;
    uint16_t keypad_state = 0;

    explicit Reader(const std::string &port);

    ~Reader();

    inline const uint8_t *get_card_uid() {
        return this->card_uid;
    }

    bool is_valid();
    bool initialize();
    bool init_crypt();

    bool read_card();

private:
    const std::string port;
    HANDLE serial_handle;
    bool valid;
    uint8_t card_uid[8];
    uint8_t reinitialized = 0, node = 1;
    uint8_t cur_msg_id = 0;
    Crypt crypt;

    inline uint8_t gen_msg_id() { return ++cur_msg_id; }

    bool set_comm_state(DWORD BaudRate);
    bool wait_for_handshake();
    bool msg_write(Message msg);

    std::vector<Message> msg_write_read(Message msg);
    std::vector<Message> msg_write_cmd_read(uint8_t cmd);
    std::vector<Message> msg_write_cmd_read(uint8_t cmd, std::vector<uint8_t> data);
    std::vector<Message> msg_read();
};

void start_reader_thread(const std::string &serial_str, int id);
void stop_reader_thread();
