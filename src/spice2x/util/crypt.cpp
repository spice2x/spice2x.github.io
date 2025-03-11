#include "crypt.h"

#include <windows.h>
#include <wincrypt.h>
#include <versionhelpers.h>

#include "util/logging.h"

namespace crypt {
    bool INITIALIZED = false;

    static HCRYPTPROV PROVIDER = 0;
    static const char *PROVIDER_XP = "Microsoft Enhanced RSA and AES Cryptographic Provider (Prototype)";
    static const char *PROVIDER_DEFAULT = "Microsoft Enhanced RSA and AES Cryptographic Provider";

    void init() {

        // determine provider name
        const char *provider;
        if (IsWindowsVistaOrGreater()) {
            provider = PROVIDER_DEFAULT;
        } else {
            provider = PROVIDER_XP;
        }

        // acquire context
        if (!CryptAcquireContext(&PROVIDER, nullptr, provider, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
            log_warning("crypt", "could not acquire context: 0x{:08x}", GetLastError());
            return;
        }

        INITIALIZED = true;
    }

    void dispose() {
        if (!INITIALIZED) {
            return;
        }

        // release context
        if (!CryptReleaseContext(PROVIDER, 0)) {
            log_warning("crypt", "could not release context");
        }
    }

    void random_bytes(void *data, size_t length) {
        CryptGenRandom(PROVIDER, (DWORD) length, (BYTE*) data);
    }

    std::string base64_encode(const uint8_t *ptr, size_t length) {
        static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        static size_t mod[] = {0, 2, 1 };
        std::string result(4 * ((length + 2) / 3), '=');
        if (ptr && length) {
            for (size_t i = 0, j = 0, triplet = 0; i < length; triplet = 0) {
                for (size_t k = 0; k < 3; ++k) {
                    triplet = (triplet << 8) | (i < length ? ptr[i++] : 0);
                }
                for (size_t k = 4; k--;) {
                    result[j++] = table[(triplet >> k * 6) & 0x3F];
                }
            }
            for (size_t i = 0; i < mod[length % 3]; i++) {
                result[result.length() - 1 - i] = '=';
            }
        }
        return result;
    }

}
