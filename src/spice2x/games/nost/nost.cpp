#include "nost.h"
#include "poke.h"
#include "hooks/setupapihook.h"
#include "touch/native/nativetouchhook.h"
#include "avs/game.h"

namespace games::nost {

    bool ENABLE_POKE = false;

    NostGame::NostGame() : Game("Nostalgia") {
    }

    void NostGame::attach() {
        Game::attach();

        // fake touchscreen
        ///////////////////

        SETUPAPI_SETTINGS touch_settings{};

        // GUID must be set to 0 to make SETUPAPI hook working
        touch_settings.class_guid[0] = 0x00000000;
        touch_settings.class_guid[1] = 0x00000000;
        touch_settings.class_guid[2] = 0x00000000;
        touch_settings.class_guid[3] = 0x00000000;

        /*
         * set hardware ID containing VID and PID
         * there is 3 known VID/PID combinations known to the game:
         * VID_04DD&PID_97CB
         * VID_0EEF&PID_C000
         * VID_29BD&PID_4101
         */
        const char touch_hardwareid[] = "VID_29BD&PID_4101";
        memcpy(touch_settings.property_hardwareid, touch_hardwareid, sizeof(touch_hardwareid));

        // apply settings
        setupapihook_init(avs::game::DLL_INSTANCE);
        setupapihook_add(touch_settings);

        nativetouch::hook(avs::game::DLL_INSTANCE);
    }

    void NostGame::post_attach() {
        if (ENABLE_POKE) {
            poke::enable();
        }
    }

    void NostGame::detach() {
        if (ENABLE_POKE) {
            poke::disable();
        }
    }
}
