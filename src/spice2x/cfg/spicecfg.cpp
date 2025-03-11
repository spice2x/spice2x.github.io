#include "spicecfg.h"

#include <memory>

#include <windows.h>
#include <shlwapi.h>

#include "launcher/launcher.h"
#include "launcher/logger.h"
#include "launcher/signal.h"
#include "launcher/options.h"
#include "util/crypt.h"
#include "util/libutils.h"
#include "rawinput/rawinput.h"

#include "config.h"
#include "configurator.h"

// for debugging, set to 1 to allocate console
#define ALLOC_CONSOLE 0

int spicecfg_run(const std::vector<std::string> &sextet_devices) {

    // initialize config
    auto &config = Config::getInstance();
    if (!config.getStatus()) {
        return EXIT_FAILURE;
    }

    // initialize input
    RI_MGR = std::make_unique<rawinput::RawInputManager>();
    RI_MGR->devices_print();
    for (const auto &device : sextet_devices) {
        RI_MGR->sextet_register(device);
    }

    // run configurator
    cfg::CONFIGURATOR_STANDALONE = true;
    cfg::Configurator configurator;
    configurator.run();

    // success
    return 0;
}

#ifdef SPICETOOLS_SPICECFG_STANDALONE
#ifdef _MSC_VER
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow) {
#else
int main(int argc, char *argv[]) {
#endif

    // initialize console
    if (ALLOC_CONSOLE) {
        AllocConsole();
        freopen("conin$", "r", stdin);
        freopen("conout$", "w", stdout);
        freopen("conout$", "w", stderr);
    }

    // run launcher with configurator option
    cfg::CONFIGURATOR_STANDALONE = true;

#ifdef _MSC_VER
    return main_implementation(__argc, __argv);
#else
    return main_implementation(argc, argv);
#endif
}
#endif
