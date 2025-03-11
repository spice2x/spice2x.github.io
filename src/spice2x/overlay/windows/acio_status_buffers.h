#pragma once

#include "overlay/window.h"
#include "acio/acio.h"

struct MemoryEditor;

namespace overlay::windows {

    class ACIOStatusBuffers : public Window {
    public:

        ACIOStatusBuffers(SpiceOverlay *overlay, acio::ACIOModule *module);
        ~ACIOStatusBuffers() override;

        void build_content() override;

    private:
        acio::ACIOModule *module;
        MemoryEditor *editor;
    };
}
