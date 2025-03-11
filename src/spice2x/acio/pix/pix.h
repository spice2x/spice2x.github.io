#pragma once

#include "../module.h"

namespace acio {

    class PIXModule : public ACIOModule {
    public:
        PIXModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
