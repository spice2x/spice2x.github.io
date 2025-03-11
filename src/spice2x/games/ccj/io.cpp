#include "io.h"

std::vector<Button> &games::ccj::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Chase Chase Jokers");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Joystick Up",
                "Joystick Down",
                "Joystick Left",
                "Joystick Right",
                "Dash",
                "Action",
                "Jump",
                "Slide",
                "Special",
                "Headphones",
                "Trackball Up",
                "Trackball Down",
                "Trackball Left",
                "Trackball Right"
        );
    }

    return buttons;
}

std::string games::ccj::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "   Dash              Action Jump Slide\n"
        " Joystick   Trackball                 Special"
        ;
}

std::vector<Analog> &games::ccj::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Chase Chase Jokers");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Joystick X",
                "Joystick Y",
                "Trackball DX",
                "Trackball DY"
        );
    }

    return analogs;
}

std::vector<Light> &games::ccj::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Chase Chase Jokers");
        GameAPI::Lights::sortLights(
                &lights,
                "Title Panel R",
                "Title Panel G",
                "Title Panel B",
                "Side Panel R",
                "Side Panel G",
                "Side Panel B",
                "Card Reader R",
                "Card Reader G",
                "Card Reader B",
                "Special Button"
        );
    }

    return lights;
}
