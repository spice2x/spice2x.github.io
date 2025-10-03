// included early to avoid warning
#include <winsock2.h>

#include "shutdown.h"

#include "api/controller.h"
#include "easrv/easrv.h"
#include "rawinput/rawinput.h"
#include "hooks/audio/audio.h"
#include "util/deferlog.h"
#include "util/logging.h"

#include "launcher.h"
#include "logger.h"
#include "nvapi/nvapi.h"

namespace launcher {

    void stop_subsystems() {
        // note that it is possible for stop_subsystems to be called multiple times
        // (e.g., crashing, and then closing the window)
        // therefore, subsystems need to be guarded against multiple unload attempts
        log_info("launcher", "stopping subsystems");

        // before shutting down logger, dump any deferred log messages
        deferredlogs::dump_to_logger();

        // flush/stop logger
        logger::stop();

        // stop ea server
        easrv_shutdown();

        // free api sockets
        if (API_CONTROLLER) {
            API_CONTROLLER->free_socket();
        }

        // notify audio hook
        hooks::audio::stop();

        // stop raw input
        if (RI_MGR) {
            RI_MGR->stop();
        }

        // unload nvapi and free library (if loaded)
        nvapi::unload();
    }

    void kill(UINT exit_code) {

        // terminate
        TerminateProcess(GetCurrentProcess(), exit_code);
    }

    void shutdown(UINT exit_code) {

        // force exit after 1s
        std::thread force_thread([exit_code] {
            Sleep(1000);
            log_info("launcher", "force shutdown");
            kill(exit_code);
            return nullptr;
        });
        force_thread.detach();

        // stop all subsystems
        stop_subsystems();

        // terminate
        kill(exit_code);
    }

    static void restart_spawn() {

        // never do this twice
        static bool already_done = false;
        if (already_done) {
            return;
        } else {
            already_done = true;
        }

        // start the process using the same args
        if (LAUNCHER_ARGC > 0) {

            // build cmd line
            std::stringstream cmd_line;
            cmd_line << "START \"\" ";
            for (int i = 0; i < LAUNCHER_ARGC; i++)
                cmd_line << " \"" << LAUNCHER_ARGV[i] << "\"";

            // run command
            system(cmd_line.str().c_str());
        }
    }

    void restart() {

        // force restart after 1s
        std::thread force_thread([] {
            Sleep(1000);
            log_info("launcher", "force restart");
            restart_spawn();
            launcher::kill(0);
            return nullptr;
        });
        force_thread.detach();

        // clean up before restart so resources can be reclaimed
        stop_subsystems();

        // spawn new and terminate this process
        restart_spawn();
        launcher::kill(0);
    }
}
