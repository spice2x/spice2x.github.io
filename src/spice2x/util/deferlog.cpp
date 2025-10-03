#include <mutex>

#include "deferlog.h"
#include "util/logging.h"

namespace deferredlogs {

    const std::initializer_list<std::string> SUPERSTEP_SOUND_ERROR_MESSAGE = {
        "audio initialization error was previously detected during boot!",
        "    (W:SuperstepSound: Audio device is not available!!!)",
        "    this crash is most likely related to audio init failure",
        "    see if the default audio device changed, fix your audio configuration (e.g., sample rate)",
        "    double check your spice audio options/patches, and try again"
    };

    std::vector<std::vector<std::string>> deferred_errors;

    void defer_error_messages(std::initializer_list<std::string> messages) {
        deferred_errors.emplace_back(messages);
    }

    void dump_to_logger() {
        static std::once_flag printed;
        std::call_once(printed, []() {
            if (!deferred_errors.empty()) {
                log_warning("deferred_error",
                    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                log_warning("deferred_error", "");
            }

            for (auto messages : deferred_errors) {
                for (auto message : messages) {
                    log_warning("deferred_error", "{}", message);
                }
                log_warning("deferred_error", "");
            }

            if (!deferred_errors.empty()) {
                log_warning("deferred_error",
                    "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            }
        });
    }
}
