#include "io.h"

#include <windows.h>

std::vector<Button> &games::udn::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("DANCE aROUND");

        const std::vector<std::string> names {
            "Service",
            "Test",
            "Coin Mech",
            "Start",
            "Up",
            "Down",
            "Left",
            "Right",
        };
        const std::vector<unsigned short> vkey_defaults {
            0xFF,
            0xFF,
            0xFF,
            VK_RETURN,
            VK_UP,
            VK_DOWN,
            VK_LEFT,
            VK_RIGHT,
        };

        buttons = GameAPI::Buttons::sortButtons(buttons, names, &vkey_defaults);
    }

    return buttons;
}

std::string games::udn::get_buttons_help() {
    return
        " Arrow keys: menu directions\n"
        " Enter: start/confirm\n";
}

std::vector<Light> &games::udn::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("DANCE aROUND");
        GameAPI::Lights::sortLights(
                &lights,
                "Card Reader R",
                "Card Reader G",
                "Card Reader B",
                "Start",
                "Up",
                "Down",
                "Left",
                "Right",
                "Title R",
                "Title G",
                "Title B",
                "Pillar Right Top R",
                "Pillar Right Top G",
                "Pillar Right Top B",
                "Pillar Right Bottom R",
                "Pillar Right Bottom G",
                "Pillar Right Bottom B",
                "Stage Left R",
                "Stage Left G",
                "Stage Left B",
                "Cabinet Left R",
                "Cabinet Left G",
                "Cabinet Left B",
                "Pillar Left Top R",
                "Pillar Left Top G",
                "Pillar Left Top B",
                "Pillar Left Bottom R",
                "Pillar Left Bottom G",
                "Pillar Left Bottom B",
                "Stage Right R",
                "Stage Right G",
                "Stage Right B",
                "Cabinet Right R",
                "Cabinet Right G",
                "Cabinet Right B"
        );
    }

    return lights;
}
