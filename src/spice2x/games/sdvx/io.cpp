#include "io.h"

std::vector<Button> &games::sdvx::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Sound Voltex");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "BT-A",
                "BT-B",
                "BT-C",
                "BT-D",
                "FX-L",
                "FX-R",
                "Start",
                "VOL-L Left",
                "VOL-L Right",
                "VOL-R Left",
                "VOL-R Right",
                "Headphone"
        );
    }

    return buttons;
}

std::string games::sdvx::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "         Start\n"
        "\n"
        " VOL-L            VOL_R\n"
        "\n"
        " BT-A  BT-B  BT-C  BT-D\n"
        "    FX-L        FX-R   \n"
        "\n"
        "For controllers, use Analog tab for knobs."
        ;
}

std::vector<Analog> &games::sdvx::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Sound Voltex");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "VOL-L",
                "VOL-R"
        );
    }

    return analogs;
}

std::vector<Light> &games::sdvx::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Sound Voltex");

        GameAPI::Lights::sortLightsWithCategory(
                &lights,
                {
                    {"Common", "BT-A"},
                    {"Common", "BT-B"},
                    {"Common", "BT-C"},
                    {"Common", "BT-D"},
                    {"Common", "FX-L"},
                    {"Common", "FX-R"},
                    {"Common", "Start"},
                    {"Nemsys", "Wing Left Up R"},
                    {"Nemsys", "Wing Left Up G"},
                    {"Nemsys", "Wing Left Up B"},
                    {"Nemsys", "Wing Right Up R"},
                    {"Nemsys", "Wing Right Up G"},
                    {"Nemsys", "Wing Right Up B"},
                    {"Nemsys", "Wing Left Low R"},
                    {"Nemsys", "Wing Left Low G"},
                    {"Nemsys", "Wing Left Low B"},
                    {"Nemsys", "Wing Right Low R"},
                    {"Nemsys", "Wing Right Low G"},
                    {"Nemsys", "Wing Right Low B"},
                    {"Nemsys", "Woofer R"},
                    {"Nemsys", "Woofer G"},
                    {"Nemsys", "Woofer B"},
                    {"Nemsys", "Controller R"},
                    {"Nemsys", "Controller G"},
                    {"Nemsys", "Controller B"},
                    {"Nemsys", "Generator R"},
                    {"Nemsys", "Generator G"},
                    {"Nemsys", "Generator B"},
                    {"Nemsys", "Pop"},
                    {"Nemsys", "Title Left"},
                    {"Nemsys", "Title Right"},
                    {"System", "Volume Sound"},
                    {"System", "Volume Headphone"},
                    {"System", "Volume External"},
                    {"System", "Volume Woofer"},
                    {"Valkyrie", "IC Card Reader R"},
                    {"Valkyrie", "IC Card Reader G"},
                    {"Valkyrie", "IC Card Reader B"},
                    {"Valkyrie", "Title Avg R"},
                    {"Valkyrie", "Title Avg G"},
                    {"Valkyrie", "Title Avg B"},
                    {"Valkyrie", "Upper Left Speaker Avg R"},
                    {"Valkyrie", "Upper Left Speaker Avg G"},
                    {"Valkyrie", "Upper Left Speaker Avg B"},
                    {"Valkyrie", "Upper Right Speaker Avg R"},
                    {"Valkyrie", "Upper Right Speaker Avg G"},
                    {"Valkyrie", "Upper Right Speaker Avg B"},
                    {"Valkyrie", "Left Wing Avg R"},
                    {"Valkyrie", "Left Wing Avg G"},
                    {"Valkyrie", "Left Wing Avg B"},
                    {"Valkyrie", "Right Wing Avg R"},
                    {"Valkyrie", "Right Wing Avg G"},
                    {"Valkyrie", "Right Wing Avg B"},
                    {"Valkyrie", "Lower Left Speaker Avg R"},
                    {"Valkyrie", "Lower Left Speaker Avg G"},
                    {"Valkyrie", "Lower Left Speaker Avg B"},
                    {"Valkyrie", "Lower Right Speaker Avg R"},
                    {"Valkyrie", "Lower Right Speaker Avg G"},
                    {"Valkyrie", "Lower Right Speaker Avg B"},
                    {"Valkyrie", "Control Panel Avg R"},
                    {"Valkyrie", "Control Panel Avg G"},
                    {"Valkyrie", "Control Panel Avg B"},
                    {"Valkyrie", "Woofer Avg R"},
                    {"Valkyrie", "Woofer Avg G"},
                    {"Valkyrie", "Woofer Avg B"},
                    {"Valkyrie", "V Unit Avg R"},
                    {"Valkyrie", "V Unit Avg G"},
                    {"Valkyrie", "V Unit Avg B"}
                }
        );
    }

    return lights;
}
