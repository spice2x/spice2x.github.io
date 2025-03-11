#pragma once

#include "../module.h"

namespace acio {

    extern uint8_t KFCA_VOL_SOUND;
    extern uint8_t KFCA_VOL_HEADPHONE;
    extern uint8_t KFCA_VOL_EXTERNAL;
    extern uint8_t KFCA_VOL_WOOFER;

    class KFCAModule : public ACIOModule {
    public:
        KFCAModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}
