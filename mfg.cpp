#include "mfg.h"

#include <format>
#include "util/fileutils.h"
#include "util/unity_player.h"
#include "util/utils.h"
#include "util/execexe.h"
#include "acioemu/handle.h"
#include "misc/wintouchemu.h"
#include "hooks/graphics/graphics.h"
#include "bi2a_hook.h"

namespace games::mfg {
    std::string MFG_INJECT_ARGS = "";
    std::string MFG_CABINET_TYPE = "HG";
    bool MFG_NO_IO = false;

    static acioemu::ACIOHandle *acioHandle = nullptr;
    static std::wstring portName;

    void MFGGame::attach() {
        Game::attach();

        // create required files
        fileutils::dir_create_recursive("dev/raw/log");
        fileutils::bin_write("dev/raw/log/output_log.txt", nullptr, 0);

        SetEnvironmentVariableA("VFG_CABINET_TYPE", MFG_CABINET_TYPE.c_str());

        // add card reader
        portName = (MFG_CABINET_TYPE == "UKS" || MFG_CABINET_TYPE == "UJK") ? std::wstring(L"\\\\.\\COM1") : std::wstring(L"\\\\.\\COM3");
        acioHandle = new acioemu::ACIOHandle(portName.c_str(), 1);
        devicehook_init_trampoline();
        devicehook_add(acioHandle);

        execexe::init();
        execexe::init_port_hook(portName, acioHandle);

        if (GRAPHICS_SHOW_CURSOR) {
            unity_utils::force_show_cursor(true);
        }

        unity_utils::set_args(
                std::format("{} {}{}",
                            GetCommandLineA(),
                            MFG_INJECT_ARGS,
                            unity_utils::get_unity_player_args()));
    }

    void MFGGame::post_attach() {
        Game::post_attach();

        execexe::load_library("libaio.dll");
        execexe::load_library("libaio-iob.dll");
        execexe::load_library("libaio-iob_video.dll");
        execexe::load_library("libaio-iob2_video.dll");
        execexe::load_library("win10actlog.dll");

        if (!MFG_NO_IO) {
            // insert BI2* hooks
            if (MFG_CABINET_TYPE == "UKS") {
                log_fatal("mfg", "UKS io is not supported");
            } else {
                bi2a_hook_init();
            }
        }
    }

    void MFGGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}