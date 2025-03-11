#pragma once

#include "../module.h"

namespace acio {

    class HGTHModule : public ACIOModule {
    public:
        HGTHModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
