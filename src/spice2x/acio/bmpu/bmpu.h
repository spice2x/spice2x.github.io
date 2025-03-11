#pragma once

#include "../module.h"

namespace acio {

    class BMPUModule : public ACIOModule {
    public:
        BMPUModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
