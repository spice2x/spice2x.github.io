#pragma once

#if SPICE64

#include "overlay/window.h"
#include <strmif.h>

namespace overlay::windows {

    class CameraControl : public Window {
    public:
        CameraControl(SpiceOverlay *overlay);
        ~CameraControl() override;

        void build_content() override;

    private:
        int m_selectedCameraIndex = 0;
        bool config_dirty = false;
        void config_save();
    };
}

#endif
