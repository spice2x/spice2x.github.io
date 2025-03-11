#include "sde.h"

#include <thread>

#include "hooks/sleephook.h"
#include "util/logging.h"

void sde_init(std::string sde_path) {

    // check trailing slash
    if (sde_path.length() > 0 && sde_path.back() == '\\') {
        sde_path.pop_back();
    }

    // get PID
    auto pid = GetCurrentProcessId();

    // try attach
    log_info("sde", "Attaching SDE ({}) to PID {}", sde_path, pid);
    std::string cmd = "sde.exe -attach-pid " + to_string(pid);
    std::thread t([sde_path, cmd]() {
        system((sde_path + "\\" + cmd).c_str());
    });
    t.detach();

    // wait to make sure it went through
    Sleep(500);
}
