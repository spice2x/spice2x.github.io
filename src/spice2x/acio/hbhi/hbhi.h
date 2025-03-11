#pragma once

#include "../module.h"

namespace acio {

    class HBHIModule : public ACIOModule {
    public:
        HBHIModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
