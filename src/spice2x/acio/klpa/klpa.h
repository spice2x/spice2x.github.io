#pragma once

#include "../module.h"

namespace acio {

    class KLPAModule : public ACIOModule {
    public:
        KLPAModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
