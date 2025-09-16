#include "pc.h"

#include <format>

#include "bi2x_hook.h"
#include "util/fileutils.h"
#include "util/unity_player.h"
#include "util/execexe.h"
#include "acioemu/handle.h"
#include "misc/wintouchemu.h"
#include "hooks/graphics/graphics.h"

namespace games::pc {
    std::string PC_INJECT_ARGS = "";
    bool PC_NO_IO = false;

    static acioemu::ACIOHandle *acioHandle = nullptr;
    static std::wstring portName = L"COM1";

    void PCGame::attach() {
        Game::attach();

        // add card reader
        acioHandle = new acioemu::ACIOHandle(portName.c_str(), 1);
        devicehook_init_trampoline();
        devicehook_add(acioHandle);

        execexe::init();
        execexe::init_deferred([] () {
            execexe::init_port_hook(portName, acioHandle);
            execexe::load_library("libaio.dll");
            execexe::load_library("libaio-iob.dll");
            execexe::load_library("libaio-iob_video.dll");
            execexe::load_library("libaio-iob2_video.dll");
            execexe::load_library("win10actlog.dll");

            if (!PC_NO_IO) {
                bi2x_hook_init();
            }
        });


        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        unity_utils::set_args(
                std::format("{} {}{}",
                            GetCommandLineA(),
                            PC_INJECT_ARGS,
                            unity_utils::get_unity_player_args()));
    }

    void PCGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}
