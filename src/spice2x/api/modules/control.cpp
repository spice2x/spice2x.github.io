#include "control.h"

#include <csignal>
#include <functional>

#include "external/rapidjson/document.h"
#include "launcher/shutdown.h"
#include "util/logging.h"
#include "util/crypt.h"
#include "util/utils.h"

using namespace std::placeholders;
using namespace rapidjson;

namespace api::modules {

    struct SignalMapping {
        int signum;
        const char* name;
    };
    static SignalMapping SIGNAL_MAPPINGS[] = {
            { SIGABRT, "SIGABRT" },
            { SIGFPE, "SIGFPE" },
            { SIGILL, "SIGILL" },
            { SIGINT, "SIGINT" },
            { SIGSEGV, "SIGSEGV" },
            { SIGTERM, "SIGTERM" },
    };

    Control::Control() : Module("control", true) {
        functions["raise"] = std::bind(&Control::raise, this, _1, _2);
        functions["exit"] = std::bind(&Control::exit, this, _1, _2);
        functions["restart"] = std::bind(&Control::restart, this, _1, _2);
        functions["session_refresh"] = std::bind(&Control::session_refresh, this, _1, _2);
        functions["shutdown"] = std::bind(&Control::shutdown, this, _1, _2);
        functions["reboot"] = std::bind(&Control::reboot, this, _1, _2);
    }

    /**
     * raise(signal: str)
     */
    void Control::raise(Request &req, Response &res) {

        // check args
        if (req.params.Size() < 1)
            return error_params_insufficient(res);
        if (!req.params[0].IsString())
            return error_type(res, "signal", "string");

        // get signal
        auto signal_str = req.params[0].GetString();
        int signal_val = -1;
        for (auto mapping : SIGNAL_MAPPINGS) {
            if (_stricmp(mapping.name, signal_str) == 0) {
                signal_val = mapping.signum;
                break;
            }
        }

        // check if not found
        if (signal_val < 0)
            return error_unknown(res, "signal", signal_str);

        // raise signal
        if (::raise(signal_val))
            return error(res, "Failed to raise signo " + to_string(signal_val));
    }

    /**
     * exit()
     * exit(code: int)
     */
    void Control::exit(Request &req, Response &res) {

        // exit()
        if (req.params.Size() == 0) {
            launcher::shutdown();
        }

        // check code
        if (!req.params[0].IsInt())
            return error_type(res, "code", "int");

        // exit
        launcher::shutdown(req.params[0].GetInt());
    }

    /**
     * restart()
     */
    void Control::restart(Request &req, Response &res) {

        // restart launcher
        launcher::restart();
    }

    /**
     * session_refresh()
     */
    void Control::session_refresh(Request &req, Response &res) {

        // generate new password
        uint8_t password_bin[128];
        crypt::random_bytes(password_bin, std::size(password_bin));
        std::string password = bin2hex(&password_bin[0], std::size(password_bin));

        // add to response
        Value password_val(password.c_str(), res.doc()->GetAllocator());
        res.add_data(password_val);

        // change password
        res.password_change(password);
    }

    /**
     * shutdown()
     */
    void Control::shutdown(Request &req, Response &res) {

        // acquire privileges
        if (!acquire_shutdown_privs())
            return error(res, "Unable to acquire shutdown privileges");

        // exit windows
        if (!ExitWindowsEx(EWX_SHUTDOWN | EWX_HYBRID_SHUTDOWN | EWX_FORCE,
                           SHTDN_REASON_MAJOR_APPLICATION |
                           SHTDN_REASON_MINOR_MAINTENANCE))
            return error(res, "Unable to shutdown system");

        // terminate this process
        launcher::shutdown(0);
    }

    /**
     * reboot()
     */
    void Control::reboot(Request &req, Response &res) {

        // acquire privileges
        if (!acquire_shutdown_privs())
            return error(res, "Unable to acquire shutdown privileges");

        // exit windows
        if (!ExitWindowsEx(EWX_REBOOT | EWX_FORCE,
                           SHTDN_REASON_MAJOR_APPLICATION |
                           SHTDN_REASON_MINOR_MAINTENANCE))
            return error(res, "Unable to reboot system");

        // terminate this process
        launcher::shutdown(0);
    }
}
