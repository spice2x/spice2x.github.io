#pragma once

#include "../module.h"

typedef enum {
    THREAD_MODE = 0,
    BACKFILL_MODE = 1
} MDXFBufferFillMode;

extern MDXFBufferFillMode BUFFER_FILL_MODE;

namespace acio {

    class MDXFModule : public ACIOModule {
    public:
        MDXFModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
        ~MDXFModule() override; 
    };
}
