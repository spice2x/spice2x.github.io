#pragma once

#include "../module.h"

namespace acio {

    class I36GModule : public ACIOModule {
    public:
        I36GModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
