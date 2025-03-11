#pragma once

#include <vector>
#include <cstdint>

class Message {
private:
    std::vector<uint8_t> encoded;
protected:
    std::vector<uint8_t> data;

    inline void reencode() { encoded.clear(); }

public:
    explicit Message(std::vector<uint8_t> data);

    uint8_t chk_sum();

    std::vector<uint8_t> get_data_encoded();

    inline std::vector<uint8_t> get_data() { return data; }

    void print_debug();
};
