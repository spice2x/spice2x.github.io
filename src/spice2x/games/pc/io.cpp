#include "io.h"

std::vector<Button> &games::pc::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Polaris Chord");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Lane 1",
                "Lane 2",
                "Lane 3",
                "Lane 4",
                "Lane 5",
                "Lane 6",
                "Lane 7",
                "Lane 8",
                "Lane 9",
                "Lane 10",
                "Lane 11",
                "Lane 12",
                "Fader L Left",
                "Fader L Right",
                "Fader R Left",
                "Fader R Right",
                "Headphone",
                "Recorder"
        );
    }

    return buttons;
}

std::string games::pc::get_buttons_help() {
    return "";
}

std::vector<Analog> &games::pc::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Polaris Chord");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Fader L",
                "Fader R"
        );
    }

    return analogs;
}

std::vector<Light> &games::pc::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Polaris Chord");

        GameAPI::Lights::sortLights(
                &lights,
                "IC Reader R",
                "IC Reader G",
                "IC Reader B"
        );
    }

    return lights;
}
