#pragma once

namespace games::gitadora {

    // installs IAT registry hooks in gfdm.dll that redirect the game's
    // ASIO driver lookup (hard-coded "XONAR" substring) to a user-chosen
    // driver name read from games::gitadora::ASIO_DRIVER.
    //
    // safe to call unconditionally; if ASIO_DRIVER is unset the hooks
    // forward every call straight through to advapi32.
    void asio_hook_init();
}
