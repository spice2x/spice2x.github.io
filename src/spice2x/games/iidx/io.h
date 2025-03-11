#pragma once

#include <vector>
#include "cfg/api.h"

namespace games::iidx {

    // all buttons in correct order
    namespace Buttons {
        enum {
            Service,
            Test,
            CoinMech,
            P1_1,
            P1_2,
            P1_3,
            P1_4,
            P1_5,
            P1_6,
            P1_7,
            P1_TTPlus,
            P1_TTMinus,
            P1_TTPlusMinus,
            P1_TTPlusMinusAlt,
            P1_Start,
            P2_1,
            P2_2,
            P2_3,
            P2_4,
            P2_5,
            P2_6,
            P2_7,
            P2_TTPlus,
            P2_TTMinus,
            P2_TTPlusMinus,
            P2_TTPlusMinusAlt,
            P2_Start,
            Effect,
            VEFX,
            P1_Headphone,
            P2_Headphone,
        };
    }

    // all analogs in correct order
    namespace Analogs {
        enum {
            TT_P1,
            TT_P2,
            VEFX,
            LowEQ,
            HiEQ,
            Filter,
            PlayVolume
        };
    }

    // all lights in correct order
    namespace Lights {
        enum {
            P1_1,
            P1_2,
            P1_3,
            P1_4,
            P1_5,
            P1_6,
            P1_7,
            P2_1,
            P2_2,
            P2_3,
            P2_4,
            P2_5,
            P2_6,
            P2_7,
            P1_Start,
            P2_Start,
            VEFX,
            Effect,
            SpotLight1,
            SpotLight2,
            SpotLight3,
            SpotLight4,
            SpotLight5,
            SpotLight6,
            SpotLight7,
            SpotLight8,
            NeonLamp,
            WooferR,
            WooferG,
            WooferB,
            ICCR_P1_R,
            ICCR_P1_G,
            ICCR_P1_B,
            ICCR_P2_R,
            ICCR_P2_G,
            ICCR_P2_B,
            TT_P1_R,
            TT_P1_G,
            TT_P1_B,
            TT_P2_R,
            TT_P2_G,
            TT_P2_B,
            TT_P1_Resistance,
            TT_P2_Resistance,
            StageLeftAvgR,
            StageLeftAvgG,
            StageLeftAvgB,
            StageRightAvgR,
            StageRightAvgG,
            StageRightAvgB,
            CabinetLeftAvgR,
            CabinetLeftAvgG,
            CabinetLeftAvgB,
            CabinetRightAvgR,
            CabinetRightAvgG,
            CabinetRightAvgB,
            ControlPanelUnderAvgR,
            ControlPanelUnderAvgG,
            ControlPanelUnderAvgB,
            CeilingLeftAvgR,
            CeilingLeftAvgG,
            CeilingLeftAvgB,
            TitleLeftAvgR,
            TitleLeftAvgG,
            TitleLeftAvgB,
            TitleRightAvgR,
            TitleRightAvgG,
            TitleRightAvgB,
            CeilingRightAvgR,
            CeilingRightAvgG,
            CeilingRightAvgB,
            TouchPanelLeftAvgR,
            TouchPanelLeftAvgG,
            TouchPanelLeftAvgB,
            TouchPanelRightAvgR,
            TouchPanelRightAvgG,
            TouchPanelRightAvgB,
            SidePanelLeftInnerAvgR,
            SidePanelLeftInnerAvgG,
            SidePanelLeftInnerAvgB,
            SidePanelLeftOuterAvgR,
            SidePanelLeftOuterAvgG,
            SidePanelLeftOuterAvgB,
            SidePanelLeftAvgR,
            SidePanelLeftAvgG,
            SidePanelLeftAvgB,
            SidePanelRightOuterAvgR,
            SidePanelRightOuterAvgG,
            SidePanelRightOuterAvgB,
            SidePanelRightInnerAvgR,
            SidePanelRightInnerAvgG,
            SidePanelRightInnerAvgB,
            SidePanelRightAvgR,
            SidePanelRightAvgG,
            SidePanelRightAvgB,
        };
    }

    // getters
    std::vector<Button> &get_buttons();
    std::string get_buttons_help();
    std::vector<Analog> &get_analogs();
    std::vector<Light> &get_lights();
}
