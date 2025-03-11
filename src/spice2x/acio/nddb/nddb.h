#pragma once

#include "../module.h"

namespace acio {

    class NDDBModule : public ACIOModule {
    public:
        NDDBModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
