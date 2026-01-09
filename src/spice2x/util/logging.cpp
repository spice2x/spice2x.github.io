#include <atomic>
#include <chrono>
#include <future>

#include "logging.h"
#include "build/defs.h"
#include "hooks/graphics/graphics.h"

std::string_view log_get_datetime() {
    return log_get_datetime(time(nullptr));
}

std::string_view log_get_datetime(std::time_t now) {
    static thread_local char buf[64];

    std::tm local_tm{};
    localtime_s(&local_tm, &now);
    strftime(buf, sizeof(buf), "[%Y/%m/%d %X]", &local_tm);

    return buf;
}

static void show_popup(const std::string text) {
    static std::once_flag shown;
    std::call_once(shown, [text]() {

        // minimize all windows
        // only needed because in multi-monitor full screen games, MessageBox fails to show above the game
        for (auto &hwnd : GRAPHICS_WINDOWS) {
            ShowWindow(hwnd, SW_FORCEMINIMIZE); 
        }

        // schedule a worker to terminate the game in 30 seconds in case user can't dismiss UI
        std::atomic_bool cancel_popup = false;
        auto worker = std::async(std::launch::async, [&cancel_popup]{
            for (int ms = 300; ms > 0; ms--) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (cancel_popup) {
                    return;
                }
            }
            launcher::stop_subsystems();
            launcher::kill();
            std::terminate();
        });

        // MessageBox will block until user presses OK
        const std::string title = "spice2x (" + to_string(VERSION_STRING_CFG) + ")";
        MessageBox(
            nullptr,
            text.c_str(),
            title.c_str(),
            MB_OK | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND | MB_TOPMOST);

        // cancel the worker since the user acknowledged the popup
        cancel_popup = true;
    });
}

void show_popup_for_crash() {
    std::string text;
    text += "Game has crashed.\n\n";
    text += "Check log.txt and look for error messages near the end of file.\n\n";
    text += "Unsure what to do next?\n";
    text += "  * update spice2x to the latest version\n";
    text += "  * check the FAQ on spice2x github wiki\n";
    text += "  * do NOT screenshot this, instead, share log.txt with someone and ask for help\n\n";
    text += "Press Enter, Esc, Alt+F4, or click OK to exit. Otherwise, game will close in 30 seconds.";
    show_popup(text);
}

void show_popup_for_fatal_error(std::string message) {
    std::string text;
    text += "A fatal error was encountered. For details, check log.txt.\n\n";
    text += "----------------------------------------------------------\n";
    text += message;
    text += "----------------------------------------------------------\n\n";
    text += "Unsure what to do next?\n";
    text += "  * update spice2x to the latest version\n";
    text += "  * check the FAQ on spice2x github wiki\n";
    text += "  * do NOT screenshot this, instead, share log.txt with someone and ask for help\n\n";
    text += "Press Enter, Esc, Alt+F4, or click OK to exit. Otherwise, game will close in 30 seconds.";
    show_popup(text);
}