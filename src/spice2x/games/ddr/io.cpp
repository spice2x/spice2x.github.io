#include "io.h"

std::vector<Button> &games::ddr::get_buttons() {
    static std::vector<Button> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Buttons::getButtons("Dance Dance Revolution");

        GameAPI::Buttons::sortButtons(
                &analogs,
                "Service",
                "Test",
                "Coin Mech",
                "P1 Start",
                "P1 Panel Up",
                "P1 Panel Down",
                "P1 Panel Left",
                "P1 Panel Right",
                "P1 Menu Up",
                "P1 Menu Down",
                "P1 Menu Left",
                "P1 Menu Right",
                "P2 Start",
                "P2 Panel Up",
                "P2 Panel Down",
                "P2 Panel Left",
                "P2 Panel Right",
                "P2 Menu Up",
                "P2 Menu Down",
                "P2 Menu Left",
                "P2 Menu Right"
        );
    }

    return analogs;
}


std::string games::ddr::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "For DDR pad arrows, double check that simultaneous Left+Right and Up+Down can be detected.\n"
        "You should boot the game, enter test menu, and use Foot Panel Check.\n\n"
        "When mapping arrows, try the following in order:\n\n"
        "  1. If your controller uses face buttons (A/B/X/Y), bind them here.\n"
        "  2. If your controller supports XInput, use that.\n"
        "  3. If your controller uses analog axis for arrows, try Analogs tab.\n"
        "  4. Otherwise, you will need to use remapping software or get a better adapter."
        ;
}

std::vector<Analog> &games::ddr::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Dance Dance Revolution");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "P1 Left-Right (Axis Fix)",
                "P1 Up-Down (Axis Fix)",
                "P2 Left-Right (Axis Fix)",
                "P2 Up-Down (Axis Fix)"
        );
    }
    return analogs;
}

std::string games::ddr::get_analogs_help() {
    // keep to max 100 characters wide
    return
        "Only use this if your DDR pad outputs analog axis for arrows.\n\n"
        "If the pad uses face buttons (A/B/X/Y), use Buttons tab instead.\n\n"
        "Spice will treat values <=25% as Left, >=75% as Right, ~=50% as neutral,\n"
        "and value between 50% and 75% as both arrows.\n\n"
        "This is the classic Stepmania \"Axis Fix\" which may or may not work with\n"
        "your dance pad or your adapter."
        ;
}

std::vector<Light> &games::ddr::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Dance Dance Revolution");

        GameAPI::Lights::sortLightsWithCategory(
            &lights,
            {
                {"General", "P1 Foot Left"},
                {"General", "P1 Foot Up"},
                {"General", "P1 Foot Right"},
                {"General", "P1 Foot Down"},
                {"General", "P2 Foot Left"},
                {"General", "P2 Foot Up"},
                {"General", "P2 Foot Right"},
                {"General", "P2 Foot Down"},

                {"SD", "Spot Red"},
                {"SD", "Spot Blue"},
                {"SD", "Top Spot Red"},
                {"SD", "Top Spot Blue"},
                {"SD", "P1 Halogen Upper"},
                {"SD", "P1 Halogen Lower"},
                {"SD", "P2 Halogen Upper"},
                {"SD", "P2 Halogen Lower"},
                {"SD", "P1 Button"},
                {"SD", "P2 Button"},
                {"SD", "Neon"},

                {"HD", "HD P1 Start"},
                {"HD", "HD P1 Menu Left-Right"},
                {"HD", "HD P1 Menu Up-Down"},
                {"HD", "HD P2 Start"},
                {"HD", "HD P2 Menu Left-Right"},
                {"HD", "HD P2 Menu Up-Down"},
                {"HD", "HD P1 Speaker F R"},
                {"HD", "HD P1 Speaker F G"},
                {"HD", "HD P1 Speaker F B"},
                {"HD", "HD P1 Speaker W R"},
                {"HD", "HD P1 Speaker W G"},
                {"HD", "HD P1 Speaker W B"},
                {"HD", "HD P2 Speaker F R"},
                {"HD", "HD P2 Speaker F G"},
                {"HD", "HD P2 Speaker F B"},
                {"HD", "HD P2 Speaker W R"},
                {"HD", "HD P2 Speaker W G"},
                {"HD", "HD P2 Speaker W B"},

                {"WHITE", "WHITE Speaker Top R"},
                {"WHITE", "WHITE Speaker Top G"},
                {"WHITE", "WHITE Speaker Top B"},
                {"WHITE", "WHITE Speaker Bottom R"},
                {"WHITE", "WHITE Speaker Bottom G"},
                {"WHITE", "WHITE Speaker Bottom B"},
                {"WHITE", "WHITE Woofer R"},
                {"WHITE", "WHITE Woofer G"},
                {"WHITE", "WHITE Woofer B"},

                {"GOLD", "GOLD P1 Menu Start"},
                {"GOLD", "GOLD P1 Menu Up"},
                {"GOLD", "GOLD P1 Menu Down"},
                {"GOLD", "GOLD P1 Menu Left"},
                {"GOLD", "GOLD P1 Menu Right"},

                {"GOLD", "GOLD P2 Menu Start"},
                {"GOLD", "GOLD P2 Menu Up"},
                {"GOLD", "GOLD P2 Menu Down"},
                {"GOLD", "GOLD P2 Menu Left"},
                {"GOLD", "GOLD P2 Menu Right"},

                {"GOLD", "GOLD Title Panel Left"},
                {"GOLD", "GOLD Title Panel Center"},
                {"GOLD", "GOLD Title Panel Right"},

                {"GOLD", "GOLD P1 Woofer Corner"},
                {"GOLD", "GOLD P2 Woofer Corner"},

                {"GOLD", "GOLD P1 Card Unit R"},
                {"GOLD", "GOLD P1 Card Unit G"},
                {"GOLD", "GOLD P1 Card Unit B"},

                {"GOLD", "GOLD P2 Card Unit R"},
                {"GOLD", "GOLD P2 Card Unit G"},
                {"GOLD", "GOLD P2 Card Unit B"},

                {"GOLD", "GOLD Top Panel Avg R"},
                {"GOLD", "GOLD Top Panel Avg G"},
                {"GOLD", "GOLD Top Panel Avg B"},

                {"GOLD", "GOLD Monitor Side Left Avg R"},
                {"GOLD", "GOLD Monitor Side Left Avg G"},
                {"GOLD", "GOLD Monitor Side Left Avg B"},

                {"GOLD", "GOLD Monitor Side Right Avg R"},
                {"GOLD", "GOLD Monitor Side Right Avg G"},
                {"GOLD", "GOLD Monitor Side Right Avg B"},

                {"GOLD", "GOLD P1 Foot Up Avg R"},
                {"GOLD", "GOLD P1 Foot Up Avg G"},
                {"GOLD", "GOLD P1 Foot Up Avg B"},

                {"GOLD", "GOLD P1 Foot Down Avg R"},
                {"GOLD", "GOLD P1 Foot Down Avg G"},
                {"GOLD", "GOLD P1 Foot Down Avg B"},

                {"GOLD", "GOLD P1 Foot Left Avg R"},
                {"GOLD", "GOLD P1 Foot Left Avg G"},
                {"GOLD", "GOLD P1 Foot Left Avg B"},

                {"GOLD", "GOLD P1 Foot Right Avg R"},
                {"GOLD", "GOLD P1 Foot Right Avg G"},
                {"GOLD", "GOLD P1 Foot Right Avg B"},

                {"GOLD", "GOLD P2 Foot Up Avg R"},
                {"GOLD", "GOLD P2 Foot Up Avg G"},
                {"GOLD", "GOLD P2 Foot Up Avg B"},

                {"GOLD", "GOLD P2 Foot Down Avg R"},
                {"GOLD", "GOLD P2 Foot Down Avg G"},
                {"GOLD", "GOLD P2 Foot Down Avg B"},

                {"GOLD", "GOLD P2 Foot Left Avg R"},
                {"GOLD", "GOLD P2 Foot Left Avg G"},
                {"GOLD", "GOLD P2 Foot Left Avg B"},

                {"GOLD", "GOLD P2 Foot Right Avg R"},
                {"GOLD", "GOLD P2 Foot Right Avg G"},
                {"GOLD", "GOLD P2 Foot Right Avg B"},

                {"GOLD", "GOLD P1 Stage Corner Up-Left"},
                {"GOLD", "GOLD P1 Stage Corner Up-Right"},
                {"GOLD", "GOLD P1 Stage Corner Down-Left"},
                {"GOLD", "GOLD P1 Stage Corner Down-Right"},

                {"GOLD", "GOLD P2 Stage Corner Up-Left"},
                {"GOLD", "GOLD P2 Stage Corner Up-Right"},
                {"GOLD", "GOLD P2 Stage Corner Down-Left"},
                {"GOLD", "GOLD P2 Stage Corner Down-Right"}
            }
        );
    }

    return lights;
}
