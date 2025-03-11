#include "io.h"

std::vector<Button> &games::jb::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Jubeat");

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
                "Button 9",
                "Button 10",
                "Button 11",
                "Button 12",
                "Button 13",
                "Button 14",
                "Button 15",
                "Button 16"
        );
    }

    return buttons;
}

std::string games::jb::get_buttons_help() {
    // keep to max 100 characters wide
    return
        " 1  2  3  4  \n"
        " 5  6  7  8  \n"
        " 9  10 11 12 \n"
        " 13 14 15 16 \n"
        "\n"
        "Touchscreen input is also supported."
        ;
}

std::vector<Light> &games::jb::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Jubeat");

        GameAPI::Lights::sortLights(
            &lights,
            "Panel Front R",
            "Panel Front G",
            "Panel Front B",
            "Panel Title R",
            "Panel Title G",
            "Panel Title B",
            "Panel Top R",
            "Panel Top G",
            "Panel Top B",
            "Panel Left R",
            "Panel Left G",
            "Panel Left B",
            "Panel Right R",
            "Panel Right G",
            "Panel Right B",
            "Panel Woofer R",
            "Panel Woofer G",
            "Panel Woofer B"
        );
    }

    return lights;
}
