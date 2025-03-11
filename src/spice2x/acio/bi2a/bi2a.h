#pragma once

#include "../module.h"

namespace acio {

    class BI2AModule : public ACIOModule {
    public:
        BI2AModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
