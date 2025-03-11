#include "bbc.h"

#include "avs/game.h"
#include "games/shared/lcdhandle.h"
#include "hooks/devicehook.h"
#include "hooks/graphics/graphics.h"
#include "util/detour.h"
#include "util/logging.h"
#include "util/sigscan.h"

namespace games::bbc {

    BBCGame::BBCGame() : Game("Bishi Bashi Channel") {
    }

    void BBCGame::attach() {
        Game::attach();

        devicehook_init();
        devicehook_add(new games::shared::LCDHandle());

        // window patch
        if (GRAPHICS_WINDOWED) {
            unsigned char pattern[] = {
                    0x48, 0x8D, 0x54, 0x24, 0x40, 0x41, 0x8D, 0x4E, 0x20, 0x41, 0x0F, 0xB6, 0xE8
            };
            unsigned char replace[] = {
                    0x48, 0x8D, 0x54, 0x24, 0x40, 0x41, 0x8D, 0x4E, 0x20, 0x31, 0xED, 0x90, 0x90
            };
            std::string mask = "XXXXXXXXXXXXX";
            if (!replace_pattern(
                    avs::game::DLL_INSTANCE,
                    pattern,
                    mask.c_str(),
                    0,
                    0,
                    replace,
                    mask.c_str()))
                log_warning("bbc", "windowed mode failed");
        }
    }


}
