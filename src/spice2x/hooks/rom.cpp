#include "rom.h"
#include "util/logging.h"
#include "util/utils.h"
#include "avs/game.h"
#include "hooks/devicehook.h"

namespace hooks::rom {

    static std::string MODEL;

    class ROMFileHandle : public CustomHandle {
    private:
        int offset = 0;

    public:

        bool open(LPCWSTR lpFileName) override {
            if (wcsicmp(lpFileName, L"D:\\001rom.txt")
            && wcsicmp(lpFileName, L"D:\\\\001rom.txt")) {
                return false;
            }
            log_info("romhook", "opened 001rom.txt");
            offset = 0;
            return true;
        }

        int read(LPVOID lpBuffer, DWORD nNumberOfBytesToRead) override {
            int ret = 0;
            for (int i = 0; i < (signed) MIN(nNumberOfBytesToRead, MODEL.length() - offset); i++) {
                *((char*) lpBuffer + i) = MODEL[i + offset];
                ret++;
            }
            offset += ret;
            if (offset == (int) MODEL.length()) {
                log_info("romhook", "read complete: {}", MODEL);
            }
            return ret;
        }

        int write(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite) override {
            return 0;
        }

        int device_io(DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                LPVOID lpOutBuffer, DWORD nOutBufferSize) override {
            return -1;
        }

        bool close() override {
            return true;
        }

        void file_info(LPBY_HANDLE_FILE_INFORMATION lpFileInformation) override {
            *lpFileInformation = BY_HANDLE_FILE_INFORMATION {};
            lpFileInformation->nFileSizeLow = MODEL.length();
        }
    };

    void init() {
        log_info("romhook", "init");

        // populate model
        if (MODEL.empty()) {
            MODEL = avs::game::MODEL;
        }

        // add device hook
        devicehook_init();
        devicehook_add(new ROMFileHandle());
    }

    void set_model(const std::string &model) {
        MODEL = model;
    }
}
