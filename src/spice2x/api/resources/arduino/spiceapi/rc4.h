#ifndef SPICEAPI_RC4_H
#define SPICEAPI_RC4_H

#include <stdint.h>
#include <stddef.h>

namespace spiceapi {

    class RC4 {
    private:
        uint8_t s_box[256];
        size_t a = 0, b = 0;

    public:

        RC4(uint8_t *key, size_t key_size);

        void crypt(uint8_t *data, size_t size);
    };
}

spiceapi::RC4::RC4(uint8_t *key, size_t key_size) {

    // initialize S-BOX
    for (size_t i = 0; i < sizeof(s_box); i++)
        s_box[i] = (uint8_t) i;

    // check key size
    if (!key_size)
        return;

    // KSA
    size_t j = 0;
    for (size_t i = 0; i < sizeof(s_box); i++) {

        // update
        j = (j + s_box[i] + key[i % key_size]) % sizeof(s_box);

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
        a = (a + 1) % sizeof(s_box);
        b = (b + s_box[a]) % sizeof(s_box);

        // swap
        auto tmp = s_box[a];
        s_box[a] = s_box[b];
        s_box[b] = tmp;

        // crypt
        data[pos] ^= s_box[(s_box[a] + s_box[b]) % sizeof(s_box)];
    }
}

#endif //SPICEAPI_RC4_H
