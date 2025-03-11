#include "io.h"

std::vector<Button> &games::popn::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Pop'n Music");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "Button 1",
                "Button 2",
                "Button 3",
                "Button 4",
                "Button 5",
                "Button 6",
                "Button 7",
                "Button 8",
                "Button 9"
        );
    }

    return buttons;
}

std::string games::popn::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "  2 4 6 8  \n"
        " 1 3 5 7 9 "
        ;
}

std::vector<Light> &games::popn::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Pop'n Music");

        GameAPI::Lights::sortLights(
                &lights,
                "Button 1",
                "Button 2",
                "Button 3",
                "Button 4",
                "Button 5",
                "Button 6",
                "Button 7",
                "Button 8",
                "Button 9",
                "Top LED 1",
                "Top LED 2",
                "Top LED 3",
                "Top LED 4",
                "Top LED 5",
                "Top LED 6",
                "Top LED 7",
                "Top LED 8",
                "Top LED 9",
                "Top LED 10",
                "Top LED 11",
                "Top LED 12",
                "Top LED 13",
                "Top LED 14",
                "Top LED 15",
                "Top LED 16",
                "Top LED 17",
                "Top LED 18",
                "Top LED 19",
                "Top LED 20",
                "Top LED 21",
                "Top LED 22",
                "Top LED 23",
                "Top LED 24",
                "Top LED 25",
                "Top LED 26",
                "Top LED 27",
                "Top LED 28",
                "Top LED 29",
                "Top LED 30",
                "Top LED 31",
                "Top LED 32",
                "Top LED R",
                "Top LED G",
                "Top LED B",
                "Hi Lamp 1",
                "Hi Lamp 2",
                "Hi Lamp 3",
                "Hi Lamp 4",
                "Hi Lamp 5",
                "Left Lamp 1",
                "Left Lamp 2",
                "Right Lamp 1",
                "Right Lamp 2",
                "Woofer LED R",
                "Woofer LED G",
                "Woofer LED B"
        );
    }

    return lights;
}
