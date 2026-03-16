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
                std::make_pair("Common", "BT-A"),
                std::make_pair("Common", "BT-B"),
                std::make_pair("Common", "BT-C"),
                std::make_pair("Common", "BT-D"),
                std::make_pair("Common", "FX-L"),
                std::make_pair("Common", "FX-R"),
                std::make_pair("Common", "Start"),
                std::make_pair("Nemsys", "Wing Left Up R"),
                std::make_pair("Nemsys", "Wing Left Up G"),
                std::make_pair("Nemsys", "Wing Left Up B"),
                std::make_pair("Nemsys", "Wing Right Up R"),
                std::make_pair("Nemsys", "Wing Right Up G"),
                std::make_pair("Nemsys", "Wing Right Up B"),
                std::make_pair("Nemsys", "Wing Left Low R"),
                std::make_pair("Nemsys", "Wing Left Low G"),
                std::make_pair("Nemsys", "Wing Left Low B"),
                std::make_pair("Nemsys", "Wing Right Low R"),
                std::make_pair("Nemsys", "Wing Right Low G"),
                std::make_pair("Nemsys", "Wing Right Low B"),
                std::make_pair("Nemsys", "Woofer R"),
                std::make_pair("Nemsys", "Woofer G"),
                std::make_pair("Nemsys", "Woofer B"),
                std::make_pair("Nemsys", "Controller R"),
                std::make_pair("Nemsys", "Controller G"),
                std::make_pair("Nemsys", "Controller B"),
                std::make_pair("Nemsys", "Generator R"),
                std::make_pair("Nemsys", "Generator G"),
                std::make_pair("Nemsys", "Generator B"),
                std::make_pair("Nemsys", "Pop"),
                std::make_pair("Nemsys", "Title Left"),
                std::make_pair("Nemsys", "Title Right"),
                std::make_pair("System", "Volume Sound"),
                std::make_pair("System", "Volume Headphone"),
                std::make_pair("System", "Volume External"),
                std::make_pair("System", "Volume Woofer"),
                std::make_pair("Valkyrie", "IC Card Reader R"),
                std::make_pair("Valkyrie", "IC Card Reader G"),
                std::make_pair("Valkyrie", "IC Card Reader B"),
                std::make_pair("Valkyrie", "Title Avg R"),
                std::make_pair("Valkyrie", "Title Avg G"),
                std::make_pair("Valkyrie", "Title Avg B"),
                std::make_pair("Valkyrie", "Upper Left Speaker Avg R"),
                std::make_pair("Valkyrie", "Upper Left Speaker Avg G"),
                std::make_pair("Valkyrie", "Upper Left Speaker Avg B"),
                std::make_pair("Valkyrie", "Upper Right Speaker Avg R"),
                std::make_pair("Valkyrie", "Upper Right Speaker Avg G"),
                std::make_pair("Valkyrie", "Upper Right Speaker Avg B"),
                std::make_pair("Valkyrie", "Left Wing Avg R"),
                std::make_pair("Valkyrie", "Left Wing Avg G"),
                std::make_pair("Valkyrie", "Left Wing Avg B"),
                std::make_pair("Valkyrie", "Right Wing Avg R"),
                std::make_pair("Valkyrie", "Right Wing Avg G"),
                std::make_pair("Valkyrie", "Right Wing Avg B"),
                std::make_pair("Valkyrie", "Lower Left Speaker Avg R"),
                std::make_pair("Valkyrie", "Lower Left Speaker Avg G"),
                std::make_pair("Valkyrie", "Lower Left Speaker Avg B"),
                std::make_pair("Valkyrie", "Lower Right Speaker Avg R"),
                std::make_pair("Valkyrie", "Lower Right Speaker Avg G"),
                std::make_pair("Valkyrie", "Lower Right Speaker Avg B"),
                std::make_pair("Valkyrie", "Control Panel Avg R"),
                std::make_pair("Valkyrie", "Control Panel Avg G"),
                std::make_pair("Valkyrie", "Control Panel Avg B"),
                std::make_pair("Valkyrie", "Woofer Avg R"),
                std::make_pair("Valkyrie", "Woofer Avg G"),
                std::make_pair("Valkyrie", "Woofer Avg B"),
                std::make_pair("Valkyrie", "V Unit Avg R"),
                std::make_pair("Valkyrie", "V Unit Avg G"),
                std::make_pair("Valkyrie", "V Unit Avg B")
        );
    }

    return lights;
}
