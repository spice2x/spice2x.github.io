#pragma once

#include "../module.h"

namespace acio {

    class PJEIModule : public ACIOModule {
    public:
        PJEIModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
