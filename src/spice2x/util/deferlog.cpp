#include <mutex>
#include <atomic>

#include "deferlog.h"
#include "util/logging.h"

namespace deferredlogs {

    const std::initializer_list<std::string> SUPERSTEP_SOUND_ERROR_MESSAGE = {
        "audio initialization error was previously detected during boot!",
        "    (W:SuperstepSound: Audio device is not available!!!)",
        "    this crash is most likely related to audio init failure",
        "      * check if the default audio device has changed",
        "      * fix your audio device settings (e.g., sample rate)",
        "      * double check your spice audio options and patches"
    };

    std::mutex deferred_errors_mutex;
    std::vector<std::vector<std::string>> deferred_errors;
    std::atomic<bool> too_late;

    void defer_error_messages(std::initializer_list<std::string> messages) {
        // don't try to acquire lock if already dumping to logger
        if (too_late) {
            return;
        }

        std::lock_guard<std::mutex> lock(deferred_errors_mutex);
        deferred_errors.emplace_back(messages);
    }

    void dump_to_logger() {
        static std::once_flag printed;
        std::call_once(printed, []() {
            too_late = true;
            if (!deferred_errors.empty()) {
                log_warning("troubleshooter", "/------------------------ spice2x auto-troubleshooter ------------------------\\");
                log_warning("troubleshooter", "");
            }

            for (auto messages : deferred_errors) {
                for (auto message : messages) {
                    log_warning("troubleshooter", "  {}", message);
                }
                log_warning("troubleshooter", "");
            }

            if (!deferred_errors.empty()) {
                log_warning("troubleshooter", "  Still unsure? Check the FAQ:");
                log_warning("troubleshooter", "      https://github.com/spice2x/spice2x.github.io/wiki/Known-issues");
                log_warning("troubleshooter", "");
                log_warning("troubleshooter", "\\------------------------ spice2x auto-troubleshooter -----------------------/");
            }
        });
    }
}
