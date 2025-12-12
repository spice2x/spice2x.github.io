#pragma once

#include "../module.h"

namespace acio {

    class MDXFModule : public ACIOModule {
    public:
        MDXFModule(HMODULE module, HookMode hookMode);
		~MDXFModule() override; 
		
        virtual void attach() override;
    };
}
