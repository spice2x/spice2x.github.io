#pragma once

#include "../module.h"

namespace acio {

    class MDXFModule : public ACIOModule {
    public:
        MDXFModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
        ~MDXFModule() override; 
    };
}
