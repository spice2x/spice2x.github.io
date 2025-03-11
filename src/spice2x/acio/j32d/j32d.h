#pragma once

#include "../module.h"

namespace acio {

    class J32DModule : public ACIOModule {
    public:
        J32DModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
