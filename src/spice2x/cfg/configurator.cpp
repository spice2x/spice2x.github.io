#include "configurator.h"

#include "overlay/overlay.h"

namespace cfg {

    // globals
    bool CONFIGURATOR_STANDALONE = false;
    ConfigType CONFIGURATOR_TYPE = ConfigType::Config;

    Configurator::Configurator() {
        CONFIGURATOR_STANDALONE = true;
    }

    Configurator::~Configurator() {
        CONFIGURATOR_STANDALONE = false;
    }

    void Configurator::run() {

        // create instance
        overlay::ENABLED = true;
        overlay::create_software(this->wnd.hWnd);
        overlay::OVERLAY->set_active(true);
        overlay::OVERLAY->hotkeys_enable = false;
        ImGui::GetIO().MouseDrawCursor = false;

        // run window
        this->wnd.run();
    }
}
