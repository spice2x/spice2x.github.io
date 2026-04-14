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
                "Headphones",
                "Button 1",
                "Button 2",
                "Button 3",
                "Button 4",
                "Button 5",
                "Button 6",
                "Button 7",
                "Button 8",
                "Button 9",
                "Red Pop-Kun",
                "Blue Pop-Kun"
        );
    }

    return buttons;
}

std::string games::popn::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "RED   2 4 6 8   BLUE\n"
        "     1 3 5 7 9"
        ;
}

std::vector<Light> &games::popn::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Pop'n Music");

        GameAPI::Lights::sortLightsWithCategory(&lights, {
                {"Common", "Button 1"},
                {"Common", "Button 2"},
                {"Common", "Button 3"},
                {"Common", "Button 4"},
                {"Common", "Button 5"},
                {"Common", "Button 6"},
                {"Common", "Button 7"},
                {"Common", "Button 8"},
                {"Common", "Button 9"},
                {"SD", "Hi Lamp 1"},
                {"SD", "Hi Lamp 2"},
                {"SD", "Hi Lamp 3"},
                {"SD", "Hi Lamp 4"},
                {"SD", "Hi Lamp 5"},
                {"SD", "Left Lamp 1"},
                {"SD", "Left Lamp 2"},
                {"SD", "Right Lamp 1"},
                {"SD", "Right Lamp 2"},
                {"HD", "Top LED 1"},
                {"HD", "Top LED 2"},
                {"HD", "Top LED 3"},
                {"HD", "Top LED 4"},
                {"HD", "Top LED 5"},
                {"HD", "Top LED 6"},
                {"HD", "Top LED 7"},
                {"HD", "Top LED 8"},
                {"HD", "Top LED 9"},
                {"HD", "Top LED 10"},
                {"HD", "Top LED 11"},
                {"HD", "Top LED 12"},
                {"HD", "Top LED 13"},
                {"HD", "Top LED 14"},
                {"HD", "Top LED 15"},
                {"HD", "Top LED 16"},
                {"HD", "Top LED 17"},
                {"HD", "Top LED 18"},
                {"HD", "Top LED 19"},
                {"HD", "Top LED 20"},
                {"HD", "Top LED 21"},
                {"HD", "Top LED 22"},
                {"HD", "Top LED 23"},
                {"HD", "Top LED 24"},
                {"HD", "Top LED 25"},
                {"HD", "Top LED 26"},
                {"HD", "Top LED 27"},
                {"HD", "Top LED 28"},
                {"HD", "Top LED 29"},
                {"HD", "Top LED 30"},
                {"HD", "Top LED 31"},
                {"HD", "Top LED 32"},
                {"HD/Pika", "Top LED R"},
                {"HD/Pika", "Top LED G"},
                {"HD/Pika", "Top LED B"},
                {"HD/Pika", "Woofer LED R"},
                {"HD/Pika", "Woofer LED G"},
                {"HD/Pika", "Woofer LED B"},
                {"Pika", "Button 1 R"},
                {"Pika", "Button 1 G"},
                {"Pika", "Button 1 B"},
                {"Pika", "Button 2 R"},
                {"Pika", "Button 2 G"},
                {"Pika", "Button 2 B"},
                {"Pika", "Button 3 R"},
                {"Pika", "Button 3 G"},
                {"Pika", "Button 3 B"},
                {"Pika", "Button 4 R"},
                {"Pika", "Button 4 G"},
                {"Pika", "Button 4 B"},
                {"Pika", "Button 5 R"},
                {"Pika", "Button 5 G"},
                {"Pika", "Button 5 B"},
                {"Pika", "Button 6 R"},
                {"Pika", "Button 6 G"},
                {"Pika", "Button 6 B"},
                {"Pika", "Button 7 R"},
                {"Pika", "Button 7 G"},
                {"Pika", "Button 7 B"},
                {"Pika", "Button 8 R"},
                {"Pika", "Button 8 G"},
                {"Pika", "Button 8 B"},
                {"Pika", "Button 9 R"},
                {"Pika", "Button 9 G"},
                {"Pika", "Button 9 B"},
                {"Pika", "Red Pop-Kun R"},
                {"Pika", "Red Pop-Kun G"},
                {"Pika", "Red Pop-Kun B"},
                {"Pika", "Blue Pop-Kun R"},
                {"Pika", "Blue Pop-Kun G"},
                {"Pika", "Blue Pop-Kun B"},
                {"Pika", "IC Card R"},
                {"Pika", "IC Card G"},
                {"Pika", "IC Card B"},
        });
    }

    return lights;
}
