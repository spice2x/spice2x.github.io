#pragma once

#include "../module.h"

namespace acio {

    class PANBModule : public ACIOModule {
    public:
        PANBModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
