#pragma once

#include "../module.h"

namespace acio {

    enum class MDXFBufferFillMode {
        // backfill mode, but if <120Hz, enable poll thread
        AUTO_MODE,

        // forces poll thread
        THREAD_MODE,

        // forces backfill mode (no poll thread)
        BACKFILL_MODE
    };

    extern MDXFBufferFillMode MDXF_BUFFER_FILL_MODE;

    class MDXFModule : public ACIOModule {
    public:
        MDXFModule(HMODULE module, HookMode hookMode);

        virtual void attach() override;
        ~MDXFModule() override; 
    };
}
