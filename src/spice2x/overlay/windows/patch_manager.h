#pragma once

#include "overlay/window.h"
#include "patcher/patch_manager.h"

namespace overlay::windows {

    class PatchManagerWindow : public Window {
    public:
        explicit PatchManagerWindow(SpiceOverlay *overlay);
        ~PatchManagerWindow() override;

        void build_content() override;

    private:
        static std::string patch_name_filter;

        void update_sorted_patches();
        void show_patch_tooltip(const patcher::PatchData& patch);
    };
}
