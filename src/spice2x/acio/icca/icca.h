#pragma once

#include "../module.h"

namespace acio {

    // settings
    extern bool ICCA_FLIP_ROWS;
    extern bool ICCA_COMPAT_ACTIVE;

    class ICCAModule : public ACIOModule {
    public:
        ICCAModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
    };
}

static inline bool is_card_uid_felica(uint8_t *uid) {
    return uid[0] != 0xE0 && uid[1] != 0x04;
}
