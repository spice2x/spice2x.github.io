#include "bc.h"

#include <format>
#include "hooks/setupapihook.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "acio2emu/handle.h"
#include "acioemu/handle.h"
#include "hooks/graphics/graphics.h"
#include "util/unity_player.h"
#include "node.h"
#include "bi2x_hook.h"

namespace games::bc {

    std::string BC_INJECT_ARGS = "";

    void BCGame::attach() {
        Game::attach();

        // create required directories
        fileutils::dir_create_recursive("dev/raw/log");

        // preload libraries
        libutils::load_library("libaio.dll");
        libutils::load_library("libaio-iob2_video.dll");

        setupapihook_init(libutils::load_library("libaio.dll"));

        SETUPAPI_SETTINGS settings {};
        const char hwid[] = "USB\\VID_1CCF&PID_8050\\0000";
        memcpy(settings.property_hardwareid, hwid, sizeof(hwid));
        const char comport[] = "COM3";
        memcpy(settings.interface_detail, comport, sizeof(comport));
        setupapihook_add(settings);

        auto iob = new acio2emu::IOBHandle(L"COM3");
        iob->register_node(std::make_unique<TBSNode>());

#if SPICE64
        // insert BI2X hooks
        bi2x_hook_init();
#endif

        devicehook_init_trampoline();
        devicehook_add(iob);
        devicehook_add(new acioemu::ACIOHandle(L"COM1"));

        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        unity_utils::set_args(
                std::format("{} {}{}",
                            GetCommandLineA(),
                            BC_INJECT_ARGS,
                            unity_utils::get_unity_player_args()));
    }

    void BCGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}
