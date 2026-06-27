#pragma once

namespace games::sdvx {

#ifdef SPICE64
    // installs the Live2D in-game scene-detection log hook used by the
    // -sdvxnolive2d "ingame" option. does not require the SDVX game module to
    // be attached, so it can be enabled purely from the launcher option.
    // only the Live2D-capable SDVX versions are 64-bit, so this is compiled out
    // of 32-bit builds.
    void live2d_scene_detection_init();
#endif

}
