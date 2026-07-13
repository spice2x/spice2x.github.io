#include "jb.h"

#include <windows.h>
#include <filesystem>

#include "cfg/configurator.h"
#include "util/logging.h"
#include "util/detour.h"
#include "util/libutils.h"

namespace games::jb {

    // fixes "IP ADDR CHANGE" errors with unusual network setups (e.g. a VPN)
    static BOOL __stdcall network_addr_is_changed() {
        return 0;
    }

    // fixes lag spikes from the periodic ping to "eamuse.konami.fun"
    static BOOL __stdcall network_get_network_check_info() {
        return 0;
    }

    // fixes network errors on non-DHCP interfaces
    static BOOL __cdecl network_get_dhcp_result() {
        return 1;
    }

    static int __cdecl GFDbgSetReportFunc(void *func) {
        log_misc("jubeat", "GFDbgSetReportFunc hook hit");

        return 0;
    }

    JBGame::JBGame() : Game("Jubeat") {
    }

    void JBGame::pre_attach() {
        if (!cfg::CONFIGURATOR_STANDALONE) {
            const auto current_path = std::filesystem::current_path();
            log_misc("jubeat", "current working directory: {}", current_path);
            if (current_path.parent_path() == current_path.root_path()) {
                log_warning(
                    "jubeat",
                    "\n\nInvalid path error; jubeat cannot run from a directory in the drive root\n"
                    "The game will overflow the stack and silently fail to boot\n\n"
                    "Instead, it must be at least two levels deep, for example:\n"
                    "    c:\\jubeat\\spice.exe           <- CRASH\n"
                    "    c:\\jubeat\\contents\\spice.exe  <- OK\n\n"
                    "To fix this, create a new directory and move ALL game files there.\n\n"
                    "Your current working directory: {}\n",
                    current_path);

                log_fatal(
                    "jubeat",
                    "Invalid path error; jubeat cannot run from a directory in the drive root");
            }
        }
    }

    void JBGame::attach() {
        Game::attach();

        touch_attach();

        // enable debug logging of gftools
        HMODULE gftools = libutils::try_module("gftools.dll");
        detour::inline_hook((void *) GFDbgSetReportFunc, libutils::try_proc(
                gftools, "GFDbgSetReportFunc"));

        // apply patches
        HMODULE network = libutils::try_module("network.dll");
        detour::inline_hook((void *) network_addr_is_changed, libutils::try_proc(
                network, "network_addr_is_changed"));
        detour::inline_hook((void *) network_get_network_check_info, libutils::try_proc(
                network, "network_get_network_check_info"));
        detour::inline_hook((void *) network_get_dhcp_result, libutils::try_proc(
                network, "network_get_dhcp_result"));
    }

    void JBGame::detach() {
        Game::detach();

        touch_detach();
    }
}
