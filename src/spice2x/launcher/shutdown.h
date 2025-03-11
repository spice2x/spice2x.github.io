#pragma once

#include <windows.h>
#include <stdlib.h>

namespace launcher {
    void stop_subsystems();
    void kill(UINT exit_code = EXIT_FAILURE);
    void shutdown(UINT exit_code = EXIT_SUCCESS);
    void restart();
}
