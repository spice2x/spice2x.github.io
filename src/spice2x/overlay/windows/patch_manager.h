#pragma once

#include "overlay/window.h"
#include "patcher/patch_manager.h"

namespace overlay::windows {

    class PatchManager : public Window {
    public:
        explicit PatchManager(SpiceOverlay *overlay);
        ~PatchManager() override;

        void build_content() override;

    private:
        static std::string patch_name_filter;

        void update_sorted_patches();
        void show_patch_tooltip(const patcher::PatchData& patch);
    };
}
