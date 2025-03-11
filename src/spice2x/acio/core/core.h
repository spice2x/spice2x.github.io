#pragma once

#include "../module.h"

namespace acio {

    class CoreModule : public ACIOModule {
    public:
        CoreModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
