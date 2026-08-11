#include "superexit.h"

#include "windows.h"

#include "cfg/configurator.h"
#include "launcher/shutdown.h"
#include "touch/touch.h"
#include "util/logging.h"

namespace superexit {

    bool has_focus() {
        HWND fg_wnd = GetForegroundWindow();
        if (fg_wnd == NULL) {
            return false;
        }
        if (fg_wnd == SPICETOUCH_TOUCH_HWND) {
            return true;
        }
        DWORD fg_pid;
        GetWindowThreadProcessId(fg_wnd, &fg_pid);
        return fg_pid == GetCurrentProcessId();
    }

    void handle_hotkeys(bool alt_f4, bool mapped_exit) {
        if ((!alt_f4 && !mapped_exit) || cfg::CONFIGURATOR_STANDALONE || !has_focus()) {
            return;
        }
        if (alt_f4) {
            log_info("superexit", "detected ALT+F4, exiting...");
            launcher::shutdown();
            return;
        }
        log_info("superexit", "detected Force Exit Game overlay shortcut, exiting...");
        launcher::shutdown();
    }
}
