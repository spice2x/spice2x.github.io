#pragma once

#include "../module.h"

namespace acio {

    class LA9AModule : public ACIOModule {
    public:
        LA9AModule(HMODULE module, HookMode hookMode);
        
        virtual void attach() override;
    };
}