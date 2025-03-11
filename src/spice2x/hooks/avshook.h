#pragma once

#include <string_view>

#include <cstdint>

#include "avs/core.h"

namespace hooks::avs {
    namespace config {
        extern bool DISABLE_VFS_DRIVE_REDIRECTION;
        extern bool LOG;
    };

    void init();
    void set_rom(const char *path, const char *contents);
}
