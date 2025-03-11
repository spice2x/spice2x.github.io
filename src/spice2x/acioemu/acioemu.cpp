#include "acioemu.h"
#include "util/logging.h"
#include "util/utils.h"

using namespace acioemu;

ACIOEmu::ACIOEmu() {
    this->devices = new std::vector<ACIODeviceEmu *>();
    this->response_buffer = new circular_buffer<uint8_t>(4096);
    this->read_buffer = new circular_buffer<uint8_t>(1024);
}

ACIOEmu::~ACIOEmu() {

    // delete devices
    for (auto device : *this->devices) {
        delete device;
    }
    delete this->devices;

    // delete buffers
    delete this->response_buffer;
    delete this->read_buffer;
}

void ACIOEmu::add_device(ACIODeviceEmu *device) {
    this->devices->push_back(device);
}

void ACIOEmu::write(uint8_t byte) {

    // insert into buffer
    if (!invert) {
        if (byte == ACIO_ESCAPE) {
            invert = true;
        } else {
            this->read_buffer->put(byte);
        }
    } else {
        byte = ~byte;
        invert = false;
        this->read_buffer->put(byte);
    }

    // clean garbage
    while (!this->read_buffer->empty() && this->read_buffer->peek() != 0xAA) {
        this->read_buffer->get();
    }
    while (this->read_buffer->size() > 1 && this->read_buffer->peek(1) == 0xAA) {
        this->read_buffer->get();
    }

    // handshake counter
    static unsigned int handshake_counter = 0;
    if (byte == 0xAA) {
        handshake_counter++;
    } else {
        handshake_counter = 0;
    }

    // check for handshake
    if (handshake_counter > 1) {

        /*
         * small hack - BIO2 seems to expect more bytes here - sending two bytes each time fixes it
         * TODO replace this handshake code with something better
         */
        this->response_buffer->put(ACIO_SOF);
        this->response_buffer->put(ACIO_SOF);
        handshake_counter--;
        return;
    }

    // parse
    if (!this->read_buffer->empty() && this->read_buffer->size() >= 6) {
        bool is_complete = false;

        // check if broadcast
        if (this->read_buffer->peek(1) == ACIO_BROADCAST) {

            // check msg data size
            auto data_size = this->read_buffer->peek(2);

            // check if msg is complete (SOF + checksum + broadcast header + data_size)
            is_complete = this->read_buffer->size() >= 2u + 2u + data_size;
        } else {

            // check msg data size
            auto data_size = this->read_buffer->peek(5);

            // check if msg is complete (SOF + checksum + command header + data_size)
            is_complete = this->read_buffer->size() >= 2u + MSG_HEADER_SIZE + data_size;
        }

        // parse message if complete
        if (is_complete) {
            this->msg_parse();
            this->read_buffer->reset();
        }
    }
}

std::optional<uint8_t> ACIOEmu::read() {
    if (this->response_buffer->empty()) {
        return std::nullopt;
    }

    return this->response_buffer->get();
}

size_t ACIOEmu::bytes_available() {
    return this->response_buffer->size();
}

void ACIOEmu::msg_parse() {

#ifdef ACIOEMU_LOG
    log_info("acioemu", "MSG RECV: {}", bin2hex(*this->read_buffer));
#endif

    // calculate checksum
    uint8_t chk = 0;
    size_t max = this->read_buffer->size() - 1;
    for (size_t i = 1; i < max; i++) {
        chk += this->read_buffer->peek(i);
    }

    // check checksum
    uint8_t chk_receive = this->read_buffer->peek(this->read_buffer->size() - 1);
    if (chk != chk_receive) {
#ifdef ACIOEMU_LOG
        log_info("acioemu", "detected wrong checksum: {}/{}", chk, chk_receive);
#endif
        return;
    }

    // get message data
    auto msg_data = this->read_buffer->peek_all();
    auto msg_in = (MessageData *) &msg_data[1];

    // correct cmd code endianness if this is not a broadcast
    if (msg_in->addr != ACIO_BROADCAST) {
        msg_in->cmd.code = acio_u16(msg_in->cmd.code);
    }

    // pass to applicable device
    uint8_t node_offset = 0;
    for (auto device : *this->devices) {
        if (device->is_applicable(node_offset, msg_in->addr)) {
            auto cur_offset = msg_in->addr - node_offset - 1;
            if (cur_offset < 0) {
                break;
            }
            if (device->parse_msg(msg_in, this->response_buffer)) {
                return;
            } else {
                break;
            }
        }
        node_offset += device->node_count;
    }

    // ignore broadcast messages by default
    if (msg_in->addr == ACIO_BROADCAST) {
        return;
    }

    /*
     * Default Behavior
     * If you want to do anything different, just handle the
     * commands in your own device implementation.
     */
    switch (msg_in->cmd.code) {

        // node count report
        case ACIO_CMD_ASSIGN_ADDRS: {
            if (msg_in->addr == 0x00 && node_offset > 0) {
                auto msg = ACIODeviceEmu::create_msg(msg_in, 1, &node_offset);
                ACIODeviceEmu::write_msg(msg, this->response_buffer);
                delete msg;
                return;
            }
            break;
        }

        // status 0 defaults
        case ACIO_CMD_CLEAR:
        case ACIO_CMD_STARTUP:
        case 0x80: // KEEPALIVE
        case 0xFF: // BROADCAST
        {
            // send status 0
            auto msg = ACIODeviceEmu::create_msg_status(msg_in, 0);
            ACIODeviceEmu::write_msg(msg, response_buffer);
            delete msg;
            return;
        }

        default:
            break;
    }

#ifdef ACIOEMU_LOG
    log_info("acioemu", "UNHANDLED MSG FOR ADDR: {}, CMD: 0x{:x}), DATA: {}",
            msg_in->addr,
            msg_in->cmd.code,
            bin2hex(*this->read_buffer));
#endif
}
