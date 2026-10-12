#include "cardconnect.h"

#include <format>

#include "acioemu/handle.h"
#include "bi2x_hook.h"
#include "printer.h"
#include "hooks/graphics/graphics.h"
#include "util/execexe.h"
#include "util/fileutils.h"
#include "util/libutils.h"
#include "util/logging.h"
#include "util/unity_player.h"

namespace games::cardconnect {
    std::string CARDCONNECT_INJECT_ARGS = "";

    static const std::wstring reader_port = L"\\\\.\\COM2";

    void CardConnectGame::attach() {
        Game::attach();

        fileutils::dir_create_recursive_log("cardconnect", "dev/raw/log");
        if (!fileutils::bin_write("dev/raw/log/output_log.txt", nullptr, 0)) {
            log_warning("cardconnect", "failed to create Unity output log");
        }

        auto *reader = new acioemu::ACIOHandle(reader_port.c_str(), 1);
        devicehook_init_trampoline();
        devicehook_add(reader);

        execexe::init();
        execexe::init_port_hook(reader_port, reader);

#if SPICE64
        libutils::load_library("libaio.dll");
        libutils::load_library("libaio-iob.dll");
        libutils::load_library("libaio-iob2_video.dll");
        bi2x_hook_init();
        printer_attach();
#endif

        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        unity_utils::set_args(
                std::format("{} {}{}",
                            GetCommandLineA(),
                            CARDCONNECT_INJECT_ARGS,
                            unity_utils::get_unity_player_args()));
    }

    void CardConnectGame::detach() {
        Game::detach();
        devicehook_dispose();
    }
}
