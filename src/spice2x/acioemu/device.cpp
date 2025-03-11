#include "device.h"

#include "util/logging.h"
#include "util/utils.h"

using namespace acioemu;

void ACIODeviceEmu::set_header(MessageData* data, uint8_t addr, uint16_t code, uint8_t pid,
        uint8_t data_size)
{
    // flag as response
    if (addr != 0) {
        addr |= ACIO_RESPONSE_FLAG;
    }

    // set header data
    data->addr = addr;
    data->cmd.code = acio_u16(code);
    data->cmd.pid = pid;
    data->cmd.data_size = data_size;
}

void ACIODeviceEmu::set_version(MessageData* data, uint32_t type, uint8_t flag,
        uint8_t ver_major, uint8_t ver_minor, uint8_t ver_rev, std::string code)
{

    // set version data
    auto data_version = &data->cmd.data_version;
    data_version->type = type;
    data_version->flag = flag;
    data_version->ver_major = ver_major;
    data_version->ver_minor = ver_minor;
    data_version->ver_rev = ver_rev;
    strncpy(data_version->code, code.c_str(), sizeof(data_version->code));
    strncpy(data_version->date, __DATE__, sizeof(data_version->date));
    strncpy(data_version->time, __TIME__, sizeof(data_version->time));
}

MessageData *ACIODeviceEmu::create_msg(uint8_t addr, uint16_t code, uint8_t pid, size_t data_size,
        uint8_t *data)
{
    // check data size
    if (data_size > 0xFF) {
        log_warning("acio", "data size > 255: {}", data_size);
        data_size = 0xFF;
    }

    // allocate data
    auto data_raw = new uint8_t[MSG_HEADER_SIZE + data_size];

    // set header
    auto msg = (MessageData *) &data_raw[0];
    set_header(msg, addr, code, pid, (uint8_t) data_size);

    // set data
    if (data) {
        memcpy(data_raw + MSG_HEADER_SIZE, data, data_size);
    } else {
        memset(data_raw + MSG_HEADER_SIZE, 0, data_size);
    }

    // return prepared message
    return msg;
}

MessageData *ACIODeviceEmu::create_msg(MessageData *msg_in, size_t data_size, uint8_t *data) {
    return create_msg(msg_in->addr, msg_in->cmd.code, msg_in->cmd.pid, data_size, data);
}

MessageData *ACIODeviceEmu::create_msg_status(uint8_t addr, uint16_t code, uint8_t pid, uint8_t status) {
    return create_msg(addr, code, pid, 1, &status);
}

MessageData *ACIODeviceEmu::create_msg_status(MessageData *msg_in, uint8_t status) {
    return create_msg_status(msg_in->addr, msg_in->cmd.code, msg_in->cmd.pid, status);
}

bool ACIODeviceEmu::is_applicable(uint8_t node_offset, uint8_t node) {
    return node > node_offset && node <= node_offset + this->node_count;
}

void ACIODeviceEmu::write_msg(const uint8_t *data, size_t size, circular_buffer<uint8_t> *response_buffer) {

    // header
    for (int i = 0; i < 2; i++) {
        response_buffer->put(ACIO_SOF);
    }

    // msg data and checksum
    uint8_t b, chk = 0;
    for (size_t i = 0; i <= size; i++) {

        // set byte to data or checksum
        if (i < size) {
            b = data[i];
            chk += b;
        } else {
            b = chk;
        }

        // check for escape
        if (b == ACIO_SOF || b == ACIO_ESCAPE) {
            response_buffer->put(ACIO_ESCAPE);
            response_buffer->put(~b);
        } else {
            response_buffer->put(b);
        }
    }

#ifdef ACIOEMU_LOG
    log_info("acioemu", "ACIO MSG OUT: AA{}{:02X}", bin2hex(data, size), chk);
#endif
}

void ACIODeviceEmu::write_msg(MessageData *msg, circular_buffer<uint8_t> *response_buffer) {
    auto data = reinterpret_cast<const uint8_t *>(msg);
    write_msg(data, MSG_HEADER_SIZE + msg->cmd.data_size, response_buffer);
}
