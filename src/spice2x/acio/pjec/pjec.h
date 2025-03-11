#pragma once

#include "../module.h"

namespace acio {

    class PJECModule : public ACIOModule {
    public:
        PJECModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
