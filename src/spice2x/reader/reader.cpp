#include "reader.h"

#include <filesystem>
#include <thread>
#include <cstring>
#include <vector>

#include "util/logging.h"
#include "misc/eamuse.h"
#include "util/utils.h"
#include "structuredmessage.h"

static std::vector<std::thread *> READER_THREADS;
static bool READER_THREAD_RUNNING = false;

Reader::Reader(const std::string &port) : port(port) {

    // open port using an NT path to support COM ports past 9
    std::filesystem::path serial_path = fmt::format("\\\\.\\{}", this->port);
    this->serial_handle = CreateFileW(
            serial_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

    // check if valid
    this->valid = this->serial_handle != INVALID_HANDLE_VALUE;
    if (!this->valid) {
        auto last_error = get_last_error_string();

        log_warning("reader", "{}: failed to open serial connection for reader: {}",
                this->port,
                last_error);
    }
}

Reader::~Reader() {
    if (this->serial_handle) {
        CloseHandle(this->serial_handle);
        log_info("reader", "closed reader on {}", this->port);
    }
}

bool Reader::is_valid() {
    return this->valid;
}

bool Reader::initialize() {
    if (!this->set_comm_state(CBR_57600) || !this->wait_for_handshake())
        return false;

    log_info("reader", "{}: card reader connected", this->port);

    // assign reader ID
    std::vector<uint8_t> set_id_data;
    set_id_data.push_back(0x00);
    if (this->msg_write_read(StructuredMessage(
            0,
            this->reinitialized,
            READER_CMD_SET_ID,
            this->gen_msg_id(),
            set_id_data)).empty())
        return false;

    // get version
    std::vector<Message> ret = this->msg_write_cmd_read(READER_CMD_VERSION);
    if (ret.empty())
        return false;

    // print version info
    std::vector<uint8_t> version_data = ret[ret.size() - 1].get_data();
    std::ostringstream model;
    model << version_data[13] << version_data[14] << version_data[15] << version_data[16];
    log_info("reader", "{}: card reader model: {}", this->port, model.str());
    std::ostringstream date;
    date << (const char *) &version_data[17];
    log_info("reader", "{}: card reader date:  {}", this->port, date.str());
    std::ostringstream clock;
    clock << (const char *) &version_data[33];
    log_info("reader", "{}: card reader clock: {}", this->port, clock.str());

    // init 2
    if (this->msg_write_cmd_read(READER_CMD_INIT2).empty())
        return false;

    // reinitialize
    this->reinitialized = 1;
    std::vector<uint8_t> reinitialize_data;
    reinitialize_data.push_back(0x00);
    if (this->msg_write_cmd_read(READER_CMD_REINITIALIZE, reinitialize_data).empty())
        return false;

    log_info("reader", "{}: card reader init done", this->port);
    return true;
}

bool Reader::init_crypt() {

    // generate game key
    std::vector<uint8_t> gk;
    for (int i = 0; i < 4; i++)
        gk.push_back((uint8_t) (rand() % 256));

    // reader crypt init
    std::vector<Message> ret = this->msg_write_cmd_read(READER_CMD_KEY_EXCHANGE, gk);
    if (ret.empty())
        return false;

    // validate message
    Message msg = ret[ret.size() - 1];
    std::vector<uint8_t> md = msg.get_data();
    if (md.size() != 9)
        return false;

    // convert keys to int32
    uint32_t game_key = gk[0] << 24 | gk[1] << 16 | gk[2] << 8 | gk[3];
    uint32_t reader_key = md[5] << 24 | md[6] << 16 | md[7] << 8 | md[8];
    log_info("reader", "{}: reader crypt client key: {}", this->port, bin2hex((char *) &gk[0], 4));
    log_info("reader", "{}: reader crypt reader key: {}", this->port, bin2hex((char *) &md[5], 4));

    // set crypt keys
    this->crypt.set_keys(reader_key, game_key);

    // crypt done
    log_info("reader", "{}: reader crypt init done", this->port);
    return true;
}

bool Reader::read_card() {

    // read card UID
    std::vector<uint8_t> status_ruid_data;
    status_ruid_data.push_back(0x00);
    status_ruid_data.push_back(0x03);
    status_ruid_data.push_back(0xFF);
    status_ruid_data.push_back(0xFF);
    if (this->msg_write_cmd_read(READER_CMD_RFID_READ_UID, status_ruid_data).empty()) {
        this->valid = false;
        return false;
    }
    Sleep(200);

    // get reader status
    std::vector<uint8_t> status_req_data;
    status_req_data.push_back(0x10);
    std::vector<Message> ret = this->msg_write_cmd_read(READER_CMD_GET_STATUS_ENC, status_req_data);
    if (ret.empty()) {
        this->valid = false;
        return false;
    }

    // get data
    Message status_msg = ret[ret.size() - 1];
    std::vector<uint8_t> status_data = status_msg.get_data();
    if (status_data.size() != 23)
        return false;

    // decrypt data
    this->crypt.crypt(&status_data[5], 18);

    // check CRC
    uint16_t crc_old = status_data[21] << 8 | status_data[22];
    uint16_t crc_new = this->crypt.crc(&status_data[5], 16);
    if (crc_old != crc_new) {
        this->valid = false;
        return false;
    }

    // get keypad state
    this->keypad_started = status_data[16];
    this->keypad_state = status_data[19] << 8 | status_data[20];

    // check for card input
    if (status_data[5] == 2) {
        memcpy(this->card_uid, &status_data[7], 8);
        return true;
    }

    return false;
}

bool Reader::set_comm_state(DWORD BaudRate) {

    // settings
    DCB serial_params{};
    serial_params.DCBlength = sizeof(serial_params);
    if (!GetCommState(this->serial_handle, &serial_params)) {
        log_warning("reader", "{}: unable to get COM port state: 0x{:x}", this->port, GetLastError());
        return false;
    }

    serial_params.BaudRate = BaudRate;
    serial_params.ByteSize = 8;
    serial_params.StopBits = ONESTOPBIT;
    serial_params.Parity = NOPARITY;
    if (!SetCommState(this->serial_handle, &serial_params)) {
        log_warning("reader", "{}: unable to set COM port state: 0x{:x}", this->port, GetLastError());
        return false;
    }

    // timeouts
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 30;
    timeouts.ReadTotalTimeoutConstant = 30;
    timeouts.ReadTotalTimeoutMultiplier = 5;
    timeouts.WriteTotalTimeoutConstant = 30;
    timeouts.WriteTotalTimeoutMultiplier = 5;
    if (!SetCommTimeouts(this->serial_handle, &timeouts)) {
        log_warning("reader", "{}: unable to set COM port timeouts: 0x{:x}", this->port, GetLastError());
        return false;
    }

    return true;
}

bool Reader::wait_for_handshake() {

    // baud rates
    DWORD baud_rates[] = { CBR_57600, CBR_38400, CBR_19200, CBR_9600 };

    // variables
    DWORD bytes_written = 0;
    DWORD bytes_read = 0;
    uint8_t read_buffer[565];

    // generate handshake buffer
    uint8_t handshake_buffer[565];
    memset(handshake_buffer, 0, 525);
    memset(handshake_buffer + 525, 0xAA, 40);

    // try all the baud rates
    for (size_t i = 0; i < 4; i++) {

        this->set_comm_state(baud_rates[i]);

        // handshake loop
        for (size_t n = 0; n < 10; n++) {

            // write handshake
            if (!WriteFile(
                        this->serial_handle,
                        handshake_buffer,
                        sizeof(handshake_buffer),
                        &bytes_written,
                        nullptr))
            {
                break;
            }

            // read handshake
            bytes_read = 0;
            if (!ReadFile(
                        this->serial_handle,
                        read_buffer,
                        sizeof(read_buffer),
                        &bytes_read,
                        nullptr))
            {
                break;
            }

            // check handshake
            if (bytes_read > 0 && read_buffer[bytes_read - 1] == 0xAA) {
                return true;
            }

            // sleep
            Sleep(50);
        }

        log_warning("reader", "{}: no handshake received for {} baud", this->port, baud_rates[i]);
    }

    // no handshake on all baud rates
    log_warning("reader", "{}: no handshake received for any attempted baud rate", this->port);
    return false;
}

bool Reader::msg_write(Message msg) {

    // get message data
    std::vector<uint8_t> msg_encoded = msg.get_data_encoded();
    uint8_t chk_sum = msg.chk_sum();

    // create write buffer
    uint8_t write_buffer[512];
    DWORD write_buffer_len = 0;

    // fill write buffer
    write_buffer[write_buffer_len++] = 0xAA;
    for (const uint8_t c : msg_encoded) {
        write_buffer[write_buffer_len++] = c;
    }

    // write checksum
    if (chk_sum == 0xAA || chk_sum == 0xFF) {
        write_buffer[write_buffer_len++] = 0xFF;
        write_buffer[write_buffer_len++] = ~chk_sum;
    } else {
        write_buffer[write_buffer_len++] = chk_sum;
    }

    // write buffer
    DWORD bytes_written = 0;
    return WriteFile(this->serial_handle,
            write_buffer,
            write_buffer_len,
            &bytes_written,
            nullptr) != 0;
}

std::vector<Message> Reader::msg_write_read(Message msg) {
    this->msg_write(msg);
    return this->msg_read();
}

std::vector<Message> Reader::msg_write_cmd_read(uint8_t cmd) {
    return this->msg_write_cmd_read(cmd, std::vector<uint8_t>());
}

std::vector<Message> Reader::msg_write_cmd_read(uint8_t cmd, std::vector<uint8_t> data) {
    this->msg_write(StructuredMessage(
            this->node,
            this->reinitialized,
            cmd,
            this->gen_msg_id(),
            data
    ));
    return this->msg_read();
}

std::vector<Message> Reader::msg_read() {

    // create buffer
    std::vector<Message> msgs;
    uint8_t read_buffer[4096];
    DWORD read_buffer_len = 0;

    // read to buffer
    if (ReadFile(
                this->serial_handle,
                read_buffer,
                sizeof(read_buffer),
                &read_buffer_len,
                nullptr) && read_buffer_len > 0)
    {
        std::vector<uint8_t> msg_data;
        size_t msg_remaining = 0;
        bool escape = false;
        for (size_t i = 0; i < read_buffer_len; i++) {
            uint8_t b = read_buffer[i];
            if (msg_remaining > 0) {

                // add msg data length
                if (msg_data.size() < 6 && msg_remaining == 1)
                    msg_remaining += msg_data[4];

                if (escape) { // escaped byte
                    b = ~b;
                    msg_data.push_back(b);
                    msg_remaining--;
                    escape = false;
                } else if (b == 0xAA) { // message start
                    msg_remaining = 6;
                    msg_data.clear();
                } else if (b == 0xFF) { // escape
                    escape = true;
                } else { // normal data
                    msg_data.push_back(b);
                    msg_remaining--;
                }
            } else if (b == 0xAA) { // message start
                msg_remaining = 6;
                msg_data.clear();
            }

            if (msg_remaining == 0 && msg_data.size() >= 6) { // message done
                std::vector<uint8_t> msg_ext_data;
                for (size_t n = 0; n < msg_data[4] && n < msg_data.size() - 6; n++) {
                    msg_ext_data.push_back(msg_data[5 + n]);
                }
                StructuredMessage msg(
                        msg_data[0],
                        msg_data[1],
                        msg_data[2],
                        msg_data[3],
                        msg_ext_data
                );
                if (msg.chk_sum() == msg_data[msg_data.size() - 1]) {
                    msgs.push_back(msg);
                }
                msg_data.clear();
            }
        }
    }

    // return message buffer
    return msgs;
}

void start_reader_thread(const std::string &port, int id) {
    READER_THREAD_RUNNING = true;
    READER_THREADS.push_back(new std::thread([port, id]() {
        log_info("reader", "{}: starting reader thread", port);

        while (READER_THREAD_RUNNING) {

            // create reader
            Reader reader(port);

            // check if serial handle is still valid
            if (!reader.is_valid()) {
                log_warning("reader", "{}: serial handle no longer valid", port);
            } else if (!reader.initialize()) {
                log_warning("reader", "{}: unable to initialize reader", port);
            } else if (reader.init_crypt()) {

                // reader loop
                while (READER_THREAD_RUNNING && reader.is_valid()) {
                    bool did_read_card = reader.read_card();

                    if (did_read_card) {
                        const uint8_t *uid = reader.get_card_uid();
                        log_info("reader", "{}: reader input: {}", port, bin2hex(uid, 8));
                        if (id >= 0) {
                            eamuse_card_insert(id, uid);
                        } else {
                            eamuse_card_insert(GetKeyState(VK_NUMLOCK) & 1, uid);
                        }
                    }

                    if (reader.keypad_started > 0) {
                        if (id >= 0) {
                            eamuse_set_keypad_overrides_reader(id, reader.keypad_state);
                        } else {
                            auto unit = GetKeyState(VK_NUMLOCK) & 1;
                            eamuse_set_keypad_overrides_reader(unit, reader.keypad_state);
                        }
                    }

                    if (did_read_card) {
                        Sleep(2500);
                    }

                    Sleep(20);
                }
            }

            // sleep between reader connection retries
            if (READER_THREAD_RUNNING)
                Sleep(5000);
        }
    }));

    // wait for thread to start
    Sleep(10);
}

void stop_reader_thread() {

    // stop threads
    if (READER_THREAD_RUNNING) {
        READER_THREAD_RUNNING = false;
    }

    // kill threads
    while (!READER_THREADS.empty()) {
        delete READER_THREADS.back();
        READER_THREADS.pop_back();
    }
}
