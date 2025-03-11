#pragma once

#include "message.h"

class StructuredMessage : public Message {
public:
    StructuredMessage(uint8_t node, uint8_t param, uint8_t cmd,
                      uint8_t packet_id, std::vector<uint8_t> request_data);
};
