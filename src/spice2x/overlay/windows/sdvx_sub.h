#ifndef SPICETOOLS_OVERLAY_WINDOWS_SDVX_SUB_H
#define SPICETOOLS_OVERLAY_WINDOWS_SDVX_SUB_H

#include <optional>

#include <windows.h>

#include "overlay/window.h"
#include "overlay/windows/generic_sub.h"

namespace overlay::windows {

    class SDVXSubScreen : public GenericSubScreen {
    public:
        SDVXSubScreen(SpiceOverlay *overlay);

    protected:
        void touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) override;
    };
}

#endif // SPICETOOLS_OVERLAY_WINDOWS_SDVX_SUB_H
