#include "dea.h"

#include "launcher/launcher.h"
#include "util/libutils.h"
#include "util/logging.h"

namespace games::dea {

    DEAGame::DEAGame() : Game("Dance Evolution") {
    }

    void DEAGame::attach() {
        Game::attach();

        // since this DLL isn't automatically loaded the hooks don't work unless we load it manually
        libutils::try_library(MODULE_PATH / "gamekdm.dll");

        // Has the user actually performed first-time setup?
        auto kinect10 = libutils::try_library("Kinect10.dll");
        if (!kinect10) {
            log_warning("dea", "Failed to load 'Kinect10.dll'. Game will boot in Test Mode. Have you installed the Kinect SDK?");
        } else {
            FreeLibrary(kinect10);
        }
    }
}
