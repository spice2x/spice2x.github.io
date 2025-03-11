#pragma once

#include <vector>

#include "cfg/api.h"

namespace games::museca {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            Start,
            Disk1Minus,
            Disk1Plus,
            Disk1Press,
            Disk2Minus,
            Disk2Plus,
            Disk2Press,
            Disk3Minus,
            Disk3Plus,
            Disk3Press,
            Disk4Minus,
            Disk4Plus,
            Disk4Press,
            Disk5Minus,
            Disk5Plus,
            Disk5Press,
            FootPedal,
            AnalogSlowdown
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            Disk1,
            Disk2,
            Disk3,
            Disk4,
            Disk5
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            TitleR,
            TitleG,
            TitleB,
            SideR,
            SideG,
            SideB,
            Spinner1R,
            Spinner1G,
            Spinner1B,
            Spinner2R,
            Spinner2G,
            Spinner2B,
            Spinner3R,
            Spinner3G,
            Spinner3B,
            Spinner4R,
            Spinner4G,
            Spinner4B,
            Spinner5R,
            Spinner5G,
            Spinner5B,
            UnderLED1R,
            UnderLED1G,
            UnderLED1B,
            UnderLED2R,
            UnderLED2G,
            UnderLED2B,
            UnderLED3R,
            UnderLED3G,
            UnderLED3B
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
