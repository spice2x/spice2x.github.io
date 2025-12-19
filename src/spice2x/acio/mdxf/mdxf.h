#pragma once

#include "../module.h"

namespace acio {
    
    typedef enum {
        THREAD_MODE = 0,
        BACKFILL_MODE = 1
    } MDXFBufferFillMode;

    extern MDXFBufferFillMode MDXF_BUFFER_FILL_MODE;

    class MDXFModule : public ACIOModule {
    public:
        MDXFModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
        ~MDXFModule() override; 
    };
}
