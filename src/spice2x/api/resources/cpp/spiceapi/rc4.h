#ifndef SPICEAPI_RC4_H
#define SPICEAPI_RC4_H

#include <cstdint>

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

#endif //SPICEAPI_RC4_H
