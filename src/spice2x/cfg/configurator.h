#pragma once

#include "configurator_wnd.h"

namespace cfg {

    enum class ConfigType {
        Config
    };

    // globals
    extern bool CONFIGURATOR_STANDALONE;
    extern ConfigType CONFIGURATOR_TYPE;

    class Configurator {
    private:
        ConfiguratorWindow wnd;

    public:

        Configurator();
        ~Configurator();
        void run();
    };
}
