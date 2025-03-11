#include "structuredmessage.h"

StructuredMessage::StructuredMessage(uint8_t node, uint8_t param, uint8_t cmd,
                                     uint8_t packet_id, std::vector<uint8_t> request_data)
        : Message(std::vector<uint8_t>()) {
    this->data.clear();
    this->data.push_back(node);
    this->data.push_back(param);
    this->data.push_back(cmd);
    this->data.push_back(packet_id);
    this->data.push_back((uint8_t) request_data.size());
    this->data.insert(this->data.end(), std::begin(request_data), std::end(request_data));
    this->reencode();
}
