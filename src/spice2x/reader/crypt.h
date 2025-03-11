#pragma once

#include <cstdint>

class Crypt {
private:
    uint32_t keys[4];

public:
    Crypt();

    void set_keys(uint32_t reader_key, uint32_t game_key);

    void crypt(uint8_t *data, size_t len);

    uint16_t crc(const uint8_t *data, size_t len);
};
