#include "icca.h"

#include "acio/icca/icca.h"
#include "avs/game.h"
#include "misc/eamuse.h"
#include "util/logging.h"
#include "util/utils.h"

using namespace acioemu;

namespace acioemu {
    bool ICCA_DEVICE_HACK = false;
}

ICCADevice::ICCADevice(bool flip_order, bool keypad_thread, uint8_t node_count) {

    // init defaults
    this->type_new = false;
    this->flip_order = flip_order;
    this->node_count = node_count;
    this->cards = new uint8_t *[node_count] {};
    this->cards_time = new time_t[node_count] {};
    this->status = new uint8_t[node_count * 16] {};
    this->accept = new bool[node_count] {};
    for (int i = 0; i < node_count; i++) {
        this->accept[i] = true;
    }
    this->hold = new bool[node_count] {};
    this->keydown = new uint8_t[node_count] {};
    this->keypad = new uint16_t[node_count] {};
    this->keypad_last = new bool*[node_count] {};
    for (int i = 0; i < node_count; i++) {
        this->keypad_last[i] = new bool[12] {};
    }
    this->keypad_capture = new uint8_t[node_count] {};
    for (int i = 0; i < node_count; i++) {
        this->keypad_capture[i] = 0x08;
    }
    this->crypt = new std::optional<Crypt>[node_count] {};
    this->counter = new uint8_t[node_count] {};
    for (int i = 0; i < node_count; i++) {
        this->counter[i] = 2;
    }

    // keypad thread for faster polling
    if (keypad_thread) {
        this->keypad_thread = new std::thread([this]() {
            while (this->cards) {
                for (int unit = 0; unit < this->node_count; unit++) {
                    this->update_keypad(unit, false);
                }
                Sleep(7);
            }
        });
    }
}

ICCADevice::~ICCADevice() {

    // stop thread
    delete keypad_thread;

    // delete cards in array
    for (int i = 0; i < node_count; i++) {
        delete cards[i];
    }

    // delete the rest
    delete[] cards;
    delete[] cards_time;
    delete[] status;
    delete[] accept;
    delete[] hold;
    delete[] keydown;
    delete[] keypad;
    delete[] keypad_last;
    delete[] keypad_capture;
    delete[] crypt;
    delete[] counter;
}

bool ICCADevice::parse_msg(MessageData *msg_in,
                           circular_buffer<uint8_t> *response_buffer) {

    // get unit
    int unit = msg_in->addr - 1;
    if (this->flip_order) {
        unit = this->node_count - unit - 1;
    }
    if (unit != 0 && unit != 1) {
        log_fatal("icca", "invalid unit: {}", unit);
    }

#ifdef ACIOEMU_LOG
    log_info("acioemu", "ICCA ADDR: {}, CMD: 0x{:04x}", unit, msg_in->cmd.code);
#endif

    // check command
    switch (msg_in->cmd.code) {
        case ACIO_CMD_GET_VERSION: {

            // send version data
            auto msg = this->create_msg(msg_in, MSG_VERSION_SIZE);
            if (
                avs::game::is_model({"LDJ", "TBS", "UJK", "XIF"}) ||
                // SDVX Valkyrie cabinet mode
                (avs::game::is_model("KFC") && (avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H'))
            ) {
                this->set_version(msg, 0x3, 0, 1, 7, 0, "ICCA");
            } else if (avs::game::is_model({"VFG"})) {
                this->set_version(msg, 0x3, 0, 1, 7, 0, "ICCB");
            } else {
                this->set_version(msg, 0x3, 0, 1, 6, 0, "ICCA");
            }
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0130: { // REINITIALIZE

            // send status 0
            auto msg = this->create_msg_status(msg_in, 0x00);
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0131: { // READ CARD UID

            // build data array
            auto msg = this->create_msg(msg_in, 16);

            // update things
            update_card(unit);
            update_keypad(unit, true);
            update_status(unit);

            // copy status
            memcpy(msg->cmd.raw, &status[unit * 16], 16);

            // explicitly set no card since this is just read
            msg->cmd.raw[0] = 0x01;

            // write message
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0135: { // SET ACTION

            // check for data
            if (msg_in->cmd.data_size >= 2) {

                // subcommand
                switch (msg_in->cmd.raw[1]) {
                    case 0x00: // ACCEPT DISABLE
                        this->accept[unit] = false;
                        break;
                    case 0x11: // ACCEPT ENABLE
                        this->accept[unit] = true;
                        break;
                    case 0x12: // EJECT
                        if (this->cards[unit] != nullptr) {
                            delete this->cards[unit];
                        }
                        this->cards[unit] = nullptr;
                        this->hold[unit] = false;
                    default:
                        break;
                }
            }

            // no break, return status
            [[fallthrough]];
        }
        case 0x0134: { // GET STATUS

            // build data array
            auto msg = this->create_msg(msg_in, 16);

            // update things
            update_card(unit);
            update_keypad(unit, true);
            update_status(unit);

            // copy status
            memcpy(msg->cmd.raw, &status[unit * 16], 16);

            // write message
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0160: { // KEY EXCHANGE

            // if this cmd is called, the reader type must be new
            this->type_new = true;

            // build data array
            auto msg = this->create_msg(msg_in, 4);

            // set key
            msg->cmd.raw[0] = 0xBE;
            msg->cmd.raw[1] = 0xEF;
            msg->cmd.raw[2] = 0xCA;
            msg->cmd.raw[3] = 0xFE;

            // convert keys
            uint32_t game_key =
                    msg_in->cmd.raw[0] << 24 |
                    msg_in->cmd.raw[1] << 16 |
                    msg_in->cmd.raw[2] << 8 |
                    msg_in->cmd.raw[3];
            uint32_t reader_key =
                    msg->cmd.raw[0] << 24 |
                    msg->cmd.raw[1] << 16 |
                    msg->cmd.raw[2] << 8 |
                    msg->cmd.raw[3];

            log_info("icca", "client key: {:08x}", game_key);
            log_info("icca", "reader key: {:08x}", reader_key);

            this->crypt[unit].emplace();
            this->crypt[unit]->set_keys(reader_key, game_key);

            // write message
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x0161: { // READ CARD UID NEW

            // if this cmd is called, the reader type must be new
            this->type_new = true;

            // decide on answer
            int answer_type = 0;
            //if (avs::game::is_model("LDJ"))
                //answer_type = 1;
            // SDVX Old cabinet mode
            if (avs::game::is_model("KFC") && avs::game::SPEC[0] != 'G' && avs::game::SPEC[0] != 'H')
                answer_type = 1;
            if (avs::game::is_model("L44"))
                answer_type = 2;

            // check answer type
            switch (answer_type) {
                case 1: {

                    // send status 1
                    auto msg = this->create_msg_status(msg_in, 1);
                    write_msg(msg, response_buffer);
                    delete msg;
                    break;
                }
                case 2: {

                    // build data array
                    auto msg = this->create_msg(msg_in, 16);

                    // update card
                    update_card(unit);

                    // check for card
                    if (this->cards[unit] != nullptr) {

                        // copy into data buffer
                        memcpy(msg->cmd.raw, this->cards[unit], 8);

                        // delete card
                        delete this->cards[unit];
                        this->cards[unit] = nullptr;
                        this->hold[unit] = false;
                    }

                    // write message
                    write_msg(msg, response_buffer);
                    delete msg;
                    break;
                }
                default: {

                    // send response with no data
                    auto msg = this->create_msg(msg_in, 0);
                    write_msg(msg, response_buffer);
                    delete msg;
                    break;
                }
            }
            break;
        }
        case 0x0164: { // GET STATUS ENC

            // build data array
            auto msg = this->create_msg(msg_in, 18);

            // update things
            update_card(unit);
            update_keypad(unit, true);
            update_status(unit);

            // copy status
            memcpy(msg->cmd.raw, &status[unit * 16], 16);

            if (this->crypt[unit].has_value()) {
                auto &crypt = this->crypt[unit];
                uint16_t crc = crypt->crc(msg->cmd.raw, 16);

                msg->cmd.raw[16] = (uint8_t) (crc >> 8);
                msg->cmd.raw[17] = (uint8_t) crc;

                crypt->crypt(msg->cmd.raw, 18);
            } else {
                log_warning("icca", "'GET STATUS ENC' message received with no crypt keys initialized");
            }

            // write message
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case 0x013A: { // POWER CONTROL (tentative name, used in 1.7 firmware)
            // TODO(felix): isolate this logic to LDJ and/or firmware 1.7 emulation

            if (this->counter[unit] > 0) {
                this->counter[unit]--;
            }
            //log_info("icca", "counter[{}] = {}", unit, this->counter[unit]);

            auto msg = this->create_msg_status(msg_in, this->counter[unit]);
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        case ACIO_CMD_STARTUP:
        case ACIO_CMD_CLEAR:
        case 0x30: // GetBoardProductNumber
        case 0x31: // GetMicomInfo
        case 0x3A: // ???
        case 0x0116: // ???
        case 0x0120: // ???
        case 0xFF: // BROADCAST
        {
            // send status 0
            auto msg = this->create_msg_status(msg_in, 0x00);
            write_msg(msg, response_buffer);
            delete msg;
            break;
        }
        default:
            return false;
    }

    // mark as handled
    return true;
}

void ICCADevice::update_card(int unit) {

    // wavepass timeout after 10s
    if (this->cards[unit] != nullptr) {
        time_t t_now;
        time(&t_now);

        if (difftime(t_now, this->cards_time[unit]) >= 10.f) {
            if (this->cards[unit] != nullptr) {
                delete this->cards[unit];
            }
            this->cards[unit] = nullptr;
            this->hold[unit] = false;
        }
    }

    bool kb_insert_press = false;

    // eamio keypress
    kb_insert_press |= static_cast<bool>(eamuse_get_keypad_state((size_t) unit) & (1 << EAM_IO_INSERT));

    // check for card
    if (this->cards[unit] == nullptr && (eamuse_card_insert_consume(this->node_count, unit) || kb_insert_press)) {
        auto card = new uint8_t[8];

        if (!eamuse_get_card(this->node_count, unit, card)) {

            // invalid card found
            delete[] card;

        } else {
            this->cards[unit] = card;
            time(&this->cards_time[unit]);
        }
    }
}

static int KEYPAD_EAMUSE_MAPPING[] = {
        0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4
};

// map for KEYPAD_KEY_CODES:
//  7 8 9  | 800 8000 8
//  4 5 6  | 400 4000 4
//  1 2 3  | 200 2000 2
//  0 00 . | 100 1000 1
static int KEYPAD_KEY_CODES[]{
        0x100,  // 0
        0x200,  // 1
        0x2000, // 2
        2,      // 3
        0x400,  // 4
        0x4000, // 5
        4,      // 6
        0x800,  // 7
        0x8000, // 8
        8,      // 9
        1,      // .
        0x1000  // 00
};

// map for KEYPAD_KEY_CODES_ALT:
//  7 8 9  | 8 80 800
//  4 5 6  | 4 40 400
//  1 2 3  | 2 20 200
//  0 00 . | 1 10 100
//
// note that the only game that needs this (SDVX VM) does not accept decimal,
// so that key is untested
static int KEYPAD_KEY_CODES_ALT[]{
        1,      // 0
        2,      // 1
        0x20,   // 2
        0x200,  // 3
        4,      // 4
        0x40,   // 5
        0x400,  // 6
        8,      // 7
        0x80,   // 8
        0x800,  // 9
        0x100,  // .
        0x10    // 00
};
static uint8_t KEYPAD_KEY_CODE_NUMS[]{
        0, 1, 5, 9, 2, 6, 10, 3, 7, 11, 8, 4
};

void ICCADevice::update_keypad(int unit, bool update_edge) {

    // lock keypad so threads can't interfere
    std::lock_guard<std::mutex> lock(this->keypad_mutex);

    // reset unit
    this->keypad[unit] = 0;

    // get eamu key states
    uint16_t eamu_state = eamuse_get_keypad_state((size_t) unit);

    // iterate keypad
    bool edge = false;
    for (int n = 0; n < 12; n++) {
        int i = n;

        // check if pressed
        if (eamu_state & (1 << KEYPAD_EAMUSE_MAPPING[i])) {

            if (ICCA_DEVICE_HACK) {
                this->keypad[unit] |= KEYPAD_KEY_CODES_ALT[i];
            } else {
                this->keypad[unit] |= KEYPAD_KEY_CODES[i];
            }

            if (!this->keypad_last[unit][i] && update_edge) {
                this->keydown[unit] = (this->keypad_capture[unit] << 4) | KEYPAD_KEY_CODE_NUMS[n];
                this->keypad_last[unit][i] = true;
                edge = true;
            }
        } else {
            this->keypad_last[unit][i] = false;
        }
    }

    // update keypad capture
    if (update_edge && edge) {
        this->keypad_capture[unit]++;
        this->keypad_capture[unit] |= 0x08;
    } else {
        this->keydown[unit] = 0;
    }
}

void ICCADevice::update_status(int unit) {

    // get buffer
    uint8_t *buffer = &this->status[unit * 16];

    // clear buffer
    memset(buffer, 0x00, 16);

    // check for card
    bool card = false;
    if (this->cards[unit] != nullptr) {

        // copy card into buffer
        memcpy(buffer + 2, this->cards[unit], 8);
        card = true;
    }

    // check for reader type
    if (this->type_new) {

        // check for card
        if (card) {

            // set status to card present
            buffer[0] = 0x02;

            /*
             * set card type
             * 0x00 - ISO15696
             * 0x01 - FELICA
             */
            bool felica = buffer[2] != 0xE0 && buffer[3] != 0x04;
            buffer[1] = felica ? 0x01 : 0x00;
            buffer[10] = felica ? 0x01 : 0x00;

        } else if (
            avs::game::is_model({"LDJ", "TBS", "XIF"}) ||
            // SDVX Valkyrie cabinet mode
            (avs::game::is_model("KFC") && (avs::game::SPEC[0] == 'G' || avs::game::SPEC[0] == 'H'))) {

            // set status to 0 otherwise reader power on fails
            buffer[0] = 0x00;
        } else {

            // set status to no card present (1 or 4)
            buffer[0] = 0x04;
        }
    } else { // old reader

        // check for card
        if (card && accept[unit]) {
            this->hold[unit] = true;
        }

        // check for hold
        if (this->hold[unit]) {

            // set status to card present
            buffer[0] = 0x02;

            /*
             * sensors
             * 0x10 - OLD READER FRONT
             * 0x20 - OLD READER BACK
             */

            // activate both sensors
            buffer[1] = 0x30;
        } else {

            // card present but reader isn't accepting it
            if (card) {

                // set card present
                buffer[0] = 0x02;

                // set front sensor
                buffer[1] = 0x10;

            } else {

                // no card present
                buffer[0] = 0x01;
            }
        }

        // card type not present for old reader
        buffer[10] = 0x00;
    }

    // other flags
    buffer[11] = 0x03;
    buffer[12] = keydown[unit];
    buffer[13] = 0x00;
    buffer[14] = (uint8_t) (keypad[unit] >> 8);
    buffer[15] = (uint8_t) (keypad[unit] & 0xFF);
}
