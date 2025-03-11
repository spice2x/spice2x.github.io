#include "io.h"

std::vector<Button> &games::qks::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("QuizKnock STADIUM");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Test",
                "Service",
                "Coin",
                "Q Button Press",
                "Q Button Sensor 1",
                "Q Button Sensor 2",
                "Q Button Sensor 3",
                "Headphones Detect",
                "Microphone Detect"
        );
    }

    return buttons;
}

std::string games::qks::get_buttons_help() {
    // keep to max 100 characters wide
    return "";
}