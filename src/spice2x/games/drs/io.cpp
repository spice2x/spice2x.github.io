#include "io.h"

std::vector<Button> &games::drs::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("DANCERUSH");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "P1 Start",
                "P1 Up",
                "P1 Down",
                "P1 Left",
                "P1 Right",
                "P2 Start",
                "P2 Up",
                "P2 Down",
                "P2 Left",
                "P2 Right"
        );
    }

    return buttons;
}

std::string games::drs::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "Motion camera required for DOWN movement.\n"
        "Touchscreen supported for dance floor."
        ;
}

std::vector<Light> &games::drs::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("DANCERUSH");
        GameAPI::Lights::sortLights(
               &lights,
               "P1 Start",
               "P1 Menu Up",
               "P1 Menu Down",
               "P1 Menu Left",
               "P1 Menu Right",
               "P2 Start",
               "P2 Menu Up",
               "P2 Menu Down",
               "P2 Menu Left",
               "P2 Menu Right",
               "Card Reader R",
               "Card Reader G",
               "Card Reader B",
               "Title Panel R",
               "Title Panel G",
               "Title Panel B"
        );
    }

    return lights;
}
