#pragma once

#include "../module.h"

namespace acio {

    class HDXSModule : public ACIOModule {
    public:
        HDXSModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
