#include "bc.h"

#include "hooks/setupapihook.h"
#include "util/libutils.h"
#include "acio2emu/handle.h"
#include "acioemu/handle.h"
#include "hooks/graphics/graphics.h"
#include "util/detour.h"
#include "node.h"

namespace games::bc {

    static decltype(ShowCursor) *ShowCursor_orig = nullptr;

    static int WINAPI ShowCursor_hook(BOOL bShow) {
        // log_misc("bsac", "ShowCursor {}", bShow);
        return 1;
    }

    void BCGame::attach() {
        Game::attach();

        setupapihook_init(libutils::load_library("libaio.dll"));

        SETUPAPI_SETTINGS settings {};
        const char hwid[] = "USB\\VID_1CCF&PID_8050\\0000";
        memcpy(settings.property_hardwareid, hwid, sizeof(hwid));
        const char comport[] = "COM3";
        memcpy(settings.interface_detail, comport, sizeof(comport));
        setupapihook_add(settings);

        auto iob = new acio2emu::IOBHandle(L"COM3");
        iob->register_node(std::make_unique<TBSNode>());

        if (GRAPHICS_SHOW_CURSOR) {
            detour::trampoline_try(
                "user32.dll", "ShowCursor",
                (void*)ShowCursor_hook, (void**)&ShowCursor_orig);
        }

        devicehook_init();
        devicehook_add(iob);
        devicehook_add(new acioemu::ACIOHandle(L"COM1"));
    }

    void BCGame::detach() {
        Game::detach();

        devicehook_dispose();
    }
}