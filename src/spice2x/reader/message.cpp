#include "message.h"
#include "util/logging.h"

Message::Message(std::vector<uint8_t> data) {
    this->data = std::move(data);
}

uint8_t Message::chk_sum() {

    // initial checksum of 0
    uint64_t chk = 0;

    // add all bytes to checksum
    for (uint8_t c : this->data)
        chk += c;

    // return checksum
    return (uint8_t) (chk & 0xFF);
}

std::vector<uint8_t> Message::get_data_encoded() {

    // check if already encoded
    if (data.size() <= encoded.size())
        return encoded;

    // encode data
    for (uint8_t c : this->data) {
        if (c == 0xAA || c == 0xFF) {
            encoded.push_back(0xFF);
            encoded.push_back(~c);
        } else {
            encoded.push_back(c);
        }
    }

    // return data
    return this->encoded;
}

void Message::print_debug() {
    std::ostringstream ss;
    for (uint8_t b : this->data)
        ss << " " << (void *) ((long long) b);
    ss << " / ";
    for (uint8_t b : this->get_data_encoded())
        ss << " " << (void *) ((long long) b);

    log_info("reader", "{}", ss.str());
}

