#include "io.h"

std::vector<Button> &games::silentscope::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Silent Scope: Bone Eater");

        GameAPI::Buttons::sortButtons(
            &buttons,
            "Service",
            "Test",
            "Coin Mech",
            "Start",
            "Up",
            "Down",
            "Left",
            "Right",
            "Gun Pressed",
            "Scope Right",
            "Scope Left"
        );
    }

    return buttons;
}

std::vector<Analog> &games::silentscope::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Silent Scope: Bone Eater");

        using GameAPI::Analogs::AnalogType;

        GameAPI::Analogs::sortAnalogsWithType(&analogs, {
            { "Gun X", AnalogType::LinearCentered },
            { "Gun Y", AnalogType::LinearCentered }
        });
    }

    return analogs;
}