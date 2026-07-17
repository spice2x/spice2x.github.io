#pragma once

#include "overlay/window.h"
#include "patcher/patch_manager.h"

namespace overlay::windows {

    class PatchManagerWindow : public Window, protected patcher::PatchManager {
    public:
        explicit PatchManagerWindow(SpiceOverlay *overlay);
        ~PatchManagerWindow() override;

        void build_content() override;

        using patcher::PatchManager::hard_apply_patches;
        using patcher::PatchManager::import_remote_patches_for_dll;
        using patcher::PatchManager::import_remote_patches_to_disk;
        using patcher::PatchManager::load_embedded_patches;
        using patcher::PatchManager::load_from_patches_json;
        using patcher::PatchManager::reload_local_patches;

    private:
        static std::string patch_name_filter;

        void update_sorted_patches();
        void show_patch_tooltip(const patcher::PatchData& patch);
    };
}
