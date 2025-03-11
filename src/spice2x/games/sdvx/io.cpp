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

        GameAPI::Lights::sortLights(
                &lights,
                "BT-A",
                "BT-B",
                "BT-C",
                "BT-D",
                "FX-L",
                "FX-R",
                "Start",
                "Wing Left Up R",
                "Wing Left Up G",
                "Wing Left Up B",
                "Wing Right Up R",
                "Wing Right Up G",
                "Wing Right Up B",
                "Wing Left Low R",
                "Wing Left Low G",
                "Wing Left Low B",
                "Wing Right Low R",
                "Wing Right Low G",
                "Wing Right Low B",
                "Woofer R",
                "Woofer G",
                "Woofer B",
                "Controller R",
                "Controller G",
                "Controller B",
                "Generator R",
                "Generator G",
                "Generator B",
                "Pop",
                "Title Left",
                "Title Right",
                "Volume Sound",
                "Volume Headphone",
                "Volume External",
                "Volume Woofer",
                "IC Card Reader R",
                "IC Card Reader G",
                "IC Card Reader B",
                "Title Avg R",
                "Title Avg G",
                "Title Avg B",
                "Upper Left Speaker Avg R",
                "Upper Left Speaker Avg G",
                "Upper Left Speaker Avg B",
                "Upper Right Speaker Avg R",
                "Upper Right Speaker Avg G",
                "Upper Right Speaker Avg B",
                "Left Wing Avg R",
                "Left Wing Avg G",
                "Left Wing Avg B",
                "Right Wing Avg R",
                "Right Wing Avg G",
                "Right Wing Avg B",
                "Lower Left Speaker Avg R",
                "Lower Left Speaker Avg G",
                "Lower Left Speaker Avg B",
                "Lower Right Speaker Avg R",
                "Lower Right Speaker Avg G",
                "Lower Right Speaker Avg B",
                "Control Panel Avg R",
                "Control Panel Avg G",
                "Control Panel Avg B",
                "Woofer Avg R",
                "Woofer Avg G",
                "Woofer Avg B",
                "V Unit Avg R",
                "V Unit Avg G",
                "V Unit Avg B"
        );
    }

    return lights;
}
