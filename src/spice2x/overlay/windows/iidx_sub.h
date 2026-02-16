#ifndef SPICETOOLS_OVERLAY_WINDOWS_IIDX_SUB_H
#define SPICETOOLS_OVERLAY_WINDOWS_IIDX_SUB_H

#include "overlay/window.h"
#include "overlay/windows/generic_sub.h"

namespace overlay::windows {

    class IIDXSubScreen : public GenericSubScreen {
    public:
        IIDXSubScreen(SpiceOverlay *overlay);

    protected:
        void touch_transform(const ImVec2 xy_in, LONG *x_out, LONG *y_out) override;
        void check_for_errors() override;
    };
}

#endif // SPICETOOLS_OVERLAY_WINDOWS_IIDX_SUB_H
