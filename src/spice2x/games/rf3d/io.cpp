#include "io.h"

std::vector<Button> &games::rf3d::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Road Fighters 3D");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "View",
                "2D/3D",
                "Lever Up",
                "Lever Down",
                "Lever Left",
                "Lever Right",
                "Wheel Left",
                "Wheel Right",
                "Accelerate",
                "Brake",
                "Auto Lever Down",
                "Auto Lever Up"
        );
    }

    return buttons;
}

std::vector<Analog> &games::rf3d::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Road Fighters 3D");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Wheel",
                "Accelerate",
                "Brake"
        );
    }

    return analogs;
}
