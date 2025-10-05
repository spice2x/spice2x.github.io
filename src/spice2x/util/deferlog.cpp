#include <mutex>
#include <atomic>

#include "deferlog.h"
#include "util/logging.h"

namespace deferredlogs {

    const std::initializer_list<std::string> SUPERSTEP_SOUND_ERROR_MESSAGE = {
        "audio initialization error was previously detected during boot!",
        "    this crash is most likely related to audio init failure",
        "      * check if the default audio device has changed",
        "      * fix your audio device settings (e.g., sample rate)",
        "      * double check your spice audio options and patches"
    };

    std::mutex deferred_errors_mutex;
    std::vector<std::vector<std::string>> deferred_errors;

    void defer_error_messages(std::initializer_list<std::string> messages) {
        std::lock_guard<std::mutex> lock(deferred_errors_mutex);
        deferred_errors.emplace_back(messages);
    }

    void dump_to_logger(bool is_crash) {
        static std::once_flag printed;
        std::call_once(printed, [is_crash]() {

            // move to a local vector under lock first
            // this is to avoid holding a lock while emitting to the logger, which may deadlock
            // due to recursive calls (e.g., via log hooks used for failure detection which then
            // again calls into defer_error_messages), and std::mutex cannot be acquired recursively
            std::vector<std::vector<std::string>> errors;
            {
                std::lock_guard<std::mutex> lock(deferred_errors_mutex);
                if (deferred_errors.empty() && !is_crash) {
                    return;
                }
                errors = std::move(deferred_errors);
            }

            log_warning("troubleshooter", "/------------------------ spice2x auto-troubleshooter ------------------------\\");
            log_warning("troubleshooter", "");
            
            if (is_crash) {
                log_warning("troubleshooter", "  the game has crashed");
                log_warning("troubleshooter", "      share this entire log file with someone for troubleshooting");
                log_warning("troubleshooter", "      spice will also attempt to create a minidump (minidump.dmp)");
                log_warning("troubleshooter", "");
            }

            for (auto messages : errors) {
                for (auto message : messages) {
                    log_warning("troubleshooter", "  {}", message);
                }
                log_warning("troubleshooter", "");
            }

            log_warning("troubleshooter", "  unsure what to do next?");
            log_warning("troubleshooter", "    * update to the latest version:");
            log_warning("troubleshooter", "        https://github.com/spice2x/spice2x.github.io/releases/latest");
            log_warning("troubleshooter", "    * check the FAQ:");
            log_warning("troubleshooter", "        https://github.com/spice2x/spice2x.github.io/wiki/Known-issues");
            log_warning("troubleshooter", "");
            log_warning("troubleshooter", "\\------------------------ spice2x auto-troubleshooter -----------------------/");
        });
    }
}
