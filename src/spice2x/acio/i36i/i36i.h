#pragma once

#include "../module.h"

namespace acio {

    class I36IModule : public ACIOModule {
    public:
        I36IModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
