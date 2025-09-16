#include "xif.h"

#include <format>
#include "util/libutils.h"
#include "util/fileutils.h"
#include "util/utils.h"
#include "util/unity_player.h"
#include "util/execexe.h"
#include "acioemu/handle.h"
#include "hooks/graphics/graphics.h"
#include "hooks/libraryhook.h"
#include "bi2x_hook.h"

namespace games::xif {
    static acioemu::ACIOHandle* acioHandle = nullptr;
    static std::wstring portName = L"COM1";

    void XIFGame::attach() {
        Game::attach();

        acioHandle = new acioemu::ACIOHandle(portName.c_str(), 1);
        execexe::init();

        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        libraryhook_enable();
        libraryhook_load_callback([](const std::string& dll) {
            static bool inited = false;
            log_info("xif", "load dll {}", dll);
            if (!inited && dll == "user32.dll") {
                log_info("xif", "init hook", dll);
                inited = true;

                auto* aio = execexe::load_library("libaio.dll");
                execexe::load_library("libaio-iob.dll");
                execexe::load_library("libaio-iob2_video.dll");

                devicehook_init(aio);
                devicehook_add(acioHandle);
                bi2x_hook_init();
            }
        });
    }

    void XIFGame::post_attach() {
        Game::post_attach();
    }

    void XIFGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}