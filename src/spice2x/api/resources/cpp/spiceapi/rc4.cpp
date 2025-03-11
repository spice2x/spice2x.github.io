#include "rc4.h"
#include <iterator>

spiceapi::RC4::RC4(uint8_t *key, size_t key_size) {

    // initialize S-BOX
    for (size_t i = 0; i < std::size(s_box); i++)
        s_box[i] = (uint8_t) i;

    // check key size
    if (!key_size)
        return;

    // KSA
    size_t j = 0;
    for (size_t i = 0; i < std::size(s_box); i++) {

        // update
        j = (j + s_box[i] + key[i % key_size]) % std::size(s_box);

        // swap
        auto tmp = s_box[i];
        s_box[i] = s_box[j];
        s_box[j] = tmp;
    }
}

void spiceapi::RC4::crypt(uint8_t *data, size_t size) {

    // iterate all bytes
    for (size_t pos = 0; pos < size; pos++) {

        // update
        a = (a + 1) % std::size(s_box);
        b = (b + s_box[a]) % std::size(s_box);

        // swap
        auto tmp = s_box[a];
        s_box[a] = s_box[b];
        s_box[b] = tmp;

        // crypt
        data[pos] ^= s_box[(s_box[a] + s_box[b]) % std::size(s_box)];
    }
}
