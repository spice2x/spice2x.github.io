#pragma once

#include "acioemu/acioemu.h"

namespace games::gitadora {
    class GitadoraDevice : public acioemu::ACIODeviceEmu {
        public:
            bool is_ready = false;
    };
}