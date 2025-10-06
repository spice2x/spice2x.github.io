#include <mutex>
#include <atomic>

#include "build/defs.h"
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

            std::string msg;
            msg += "\n\n";
            msg += "/-------------------------- spice2x auto-troubleshooter -----------------------\\\n";
            msg += "\n";
            msg += "  spice2x version: " + to_string(VERSION_STRING_CFG) + "\n";
            msg += "\n";
            
            if (is_crash) {
                msg += "  the game has crashed\n";
                msg += "      share this entire log file with someone for troubleshooting (log.txt)\n";
                msg += "      spice will also attempt to create a minidump (minidump.dmp)\n";
                msg += "\n";
            }

            for (auto messages : errors) {
                for (auto message : messages) {
                    msg += "  " + message + "\n";
                }
                msg += "\n";
            }

            msg += "  unsure what to do next?\n";
            msg += "    * update to the latest version:\n";
            msg += "        https://github.com/spice2x/spice2x.github.io/releases/latest\n";
            msg += "    * check the FAQ:\n";
            msg += "        https://github.com/spice2x/spice2x.github.io/wiki/Known-issues\n";
            msg += "\n";
            msg += "\\------------------------- spice2x auto-troubleshooter ------------------------/\n";

            log_warning("troubleshooter", "{}", msg);
        });
    }
}
