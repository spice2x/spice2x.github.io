#include <atomic>
#include <future>

#include "logging.h"
#include "build/defs.h"
#include "hooks/graphics/graphics.h"

std::string_view log_get_datetime() {
    return log_get_datetime(time(nullptr));
}

std::string_view log_get_datetime(std::time_t now) {
    static thread_local char buf[64];

    // `localtime` on Windows is thread-safe
    strftime(buf, sizeof(buf), "[%Y/%m/%d %X]", localtime(&now));

    return buf;
}

static void show_popup(const std::string text) {
    static std::once_flag shown;
    std::call_once(shown, [text]() {

        auto future = std::async(std::launch::async, [text] {
            // minimize all windows
            // only needed because in multi-monitor full screen games, MessageBox fails to show above the game
            for (auto &hwnd : GRAPHICS_WINDOWS) {
                ShowWindow(hwnd, SW_FORCEMINIMIZE); 
            }

            // MessageBox will block until user presses OK
            const std::string title = "spice2x (" + to_string(VERSION_STRING_CFG) + ")";
            MessageBox(
                nullptr,
                text.c_str(),
                title.c_str(),
                MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND | MB_TOPMOST);
        });

        // show popup, but don't wait forever for user
        future.wait_for(std::chrono::seconds(30));
    });
}

void show_popup_for_crash() {
    std::string text;
    text += "Game has crashed.\n\n";
    text += "Check log.txt and look for error messages near the end of file.\n\n";
    text += "Unsure what to do next?\n";
    text += "  * update spice2x to the latest version\n";
    text += "  * check the FAQ on spice2x wiki on github\n";
    text += "  * do NOT screenshot this, instead, share log.txt with someone and ask for help\n\n";
    text += "Press Enter, Esc, Alt+F4, or click OK to exit. Otherwise, game will close in 30 seconds.";
    show_popup(text);
}

void show_popup_for_fatal_error(std::string message) {
    std::string text;
    text += "A fatal error was encountered. For details, check log.txt.\n\n";
    text += message;
    text += "\n";
    text += "Unsure what to do next?\n";
    text += "  * update spice2x to the latest version\n";
    text += "  * check the FAQ on spice2x wiki on github\n";
    text += "  * do NOT screenshot this, instead, share log.txt with someone and ask for help\n\n";
    text += "Press Enter, Esc, Alt+F4, or click OK to exit. Otherwise, game will close in 30 seconds.";
    show_popup(text);
}