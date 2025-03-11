#include "debughook.h"
#include "util/utils.h"
#include "util/detour.h"
#include "util/libutils.h"
#include "avs/core.h"
#include "avs/ea3.h"
#include "avs/game.h"

namespace debughook {

    // settings
    bool DEBUGHOOK_LOGGING = true;

    // function pointers
    static decltype(OutputDebugStringA) *OutputDebugStringA_orig = nullptr;
    static decltype(OutputDebugStringW) *OutputDebugStringW_orig = nullptr;

    static void WINAPI OutputDebugStringA_hook(LPCTSTR str) {

        // check if logging is enabled
        if (!DEBUGHOOK_LOGGING) {
            return;
        }

        // create buffer
        auto len = strlen(str);
        auto buf = new TCHAR[len + 1];
        memset(buf, 0, len + 1);

        // copy to buffer, log message on new lines
        size_t buf_i = 0;
        for (size_t i = 0; str[i] != 0 && i < len; i++) {
            if (str[i] == '\r') {

                // skip carriage return
                continue;
            } else if (str[i] == '\n') {

                // null terminate buffer
                buf[buf_i] = '\0';

                // log buffer
                log_info("debughook", "{}", buf);

                // reset buffer
                len -= buf_i;
                buf_i = 0;
                memset(buf, 0, len + 1);
            } else {
                buf[buf_i] = str[i];
                buf_i++;
            }
        }

        // log buffer if there are remaining characters
        if (buf_i > 0) {
            log_info("debughook", "{}", buf);
        }

        // delete buffer
        delete[] buf;
    }

    static void WINAPI OutputDebugStringW_hook(const wchar_t *str) {

        // check if logging is enabled
        if (!DEBUGHOOK_LOGGING) {
            return;
        }

        OutputDebugStringA_hook(ws2s(str).c_str());
    }

    void attach() {
        log_info("debughook", "attaching...");

        HMODULE kernel32 = libutils::try_module("kernel32.dll");
        detour::inline_hook((void *) OutputDebugStringA_hook,
                            libutils::try_proc(kernel32, "OutputDebugStringA"));
        detour::inline_hook((void *) OutputDebugStringW_hook,
                            libutils::try_proc(kernel32, "OutputDebugStringW"));
        OutputDebugStringA_orig = detour::iat_try(
                "OutputDebugStringA", OutputDebugStringA_hook, nullptr);
        OutputDebugStringW_orig = detour::iat_try(
                "OutputDebugStringW", OutputDebugStringW_hook, nullptr);

        log_info("debughook", "attached");
    }

    void detach() {
        log_info("debughook", "detaching...");

        if (OutputDebugStringA_orig != nullptr) {
            detour::iat_try("OutputDebugStringA", OutputDebugStringA_orig, nullptr);
        }
        if (OutputDebugStringW_orig != nullptr) {
            detour::iat_try("OutputDebugStringW", OutputDebugStringW_orig, nullptr);
        }

        log_info("debughook", "detached");
    }

}
