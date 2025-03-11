#include "bs.h"
#include "avs/game.h"
#include "misc/wintouchemu.h"
#include "launcher/launcher.h"
#include "rawinput/rawinput.h"
#include "util/detour.h"
#include "util/logging.h"

namespace games::bs {

    BSGame::BSGame() : Game("Beatstream") {
    }

    inline bool touchscreen_has_bug() {

        // get devices
        auto devices = RI_MGR->devices_get();
        for (auto &device : devices) {

            // filter by HID
            if (device.type == rawinput::HID) {
                auto &hid = device.hidInfo;
                auto &attributes = hid->attributes;

                // P2314T
                // it apparently cannot do holds using Beatstream's wintouch code
                if (attributes.VendorID == 0x2149 && attributes.ProductID == 0x2316)
                    return true;

                // P2418HT
                // it apparently cannot do holds using Beatstream's wintouch code
                if (attributes.VendorID == 0x1FD2 && attributes.ProductID == 0x6103)
                    return true;
            }
        }

        // looks all clean
        return false;
    }

    void BSGame::attach() {
        Game::attach();

        // for bugged touch screens
        if (touchscreen_has_bug()) {
            log_info("bs", "detected bugged touchscreen, forcing wintouchemu");
            wintouchemu::FORCE = true;
        }

        // for mouse support
        wintouchemu::hook("BeatStream", avs::game::DLL_INSTANCE);
    }
}
