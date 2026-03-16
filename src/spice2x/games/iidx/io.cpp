#include "io.h"

std::vector<Button> &games::iidx::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("Beatmania IIDX");

        GameAPI::Buttons::sortButtons(
                &buttons,
                "Service",
                "Test",
                "Coin Mech",
                "P1 1",
                "P1 2",
                "P1 3",
                "P1 4",
                "P1 5",
                "P1 6",
                "P1 7",
                "P1 TT+",
                "P1 TT-",
                "P1 TT+/-",
                "P1 TT+/- Alternate",
                "P1 Start",
                "P2 1",
                "P2 2",
                "P2 3",
                "P2 4",
                "P2 5",
                "P2 6",
                "P2 7",
                "P2 TT+",
                "P2 TT-",
                "P2 TT+/-",
                "P2 TT+/- Alternate",
                "P2 Start",
                "EFFECT",
                "VEFX",
                "P1 Headphone",
                "P2 Headphone"
        );
    }

    return buttons;
}

std::string games::iidx::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "           P1   EFCT    P2          \n"
        "                VEFX                \n"
        "       2 4 6            2 4 6       \n"
        "  TT  1 3 5 7          1 3 5 7   TT \n"
        "\n"
        "For controllers, use Analog tab for turntable (TT)."
        ;
}

std::vector<Analog> &games::iidx::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("Beatmania IIDX");

        GameAPI::Analogs::sortAnalogs(
                &analogs,
                "Turntable P1",
                "Turntable P2",
                "VEFX",
                "Low-EQ",
                "Hi-EQ",
                "Filter",
                "Play Volume"
        );
    }
    return analogs;
}

std::vector<Light> &games::iidx::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Beatmania IIDX");

        GameAPI::Lights::sortLightsWithCategory(
                &lights,
                std::make_pair("Common", "P1 1"),
                std::make_pair("Common", "P1 2"),
                std::make_pair("Common", "P1 3"),
                std::make_pair("Common", "P1 4"),
                std::make_pair("Common", "P1 5"),
                std::make_pair("Common", "P1 6"),
                std::make_pair("Common", "P1 7"),
                std::make_pair("Common", "P2 1"),
                std::make_pair("Common", "P2 2"),
                std::make_pair("Common", "P2 3"),
                std::make_pair("Common", "P2 4"),
                std::make_pair("Common", "P2 5"),
                std::make_pair("Common", "P2 6"),
                std::make_pair("Common", "P2 7"),
                std::make_pair("Common", "P1 Start"),
                std::make_pair("Common", "P2 Start"),
                std::make_pair("Common", "VEFX"),
                std::make_pair("Common", "Effect"),
                std::make_pair("DX", "Spot Light 1"),
                std::make_pair("DX", "Spot Light 2"),
                std::make_pair("DX", "Spot Light 3"),
                std::make_pair("DX", "Spot Light 4"),
                std::make_pair("DX", "Spot Light 5"),
                std::make_pair("DX", "Spot Light 6"),
                std::make_pair("DX", "Spot Light 7"),
                std::make_pair("DX", "Spot Light 8"),
                std::make_pair("DX", "Neon Lamp"),
                std::make_pair("Lightning Model", "Woofer R"),
                std::make_pair("Lightning Model", "Woofer G"),
                std::make_pair("Lightning Model", "Woofer B"),
                std::make_pair("Lightning Model", "IC Card Reader P1 R"),
                std::make_pair("Lightning Model", "IC Card Reader P1 G"),
                std::make_pair("Lightning Model", "IC Card Reader P1 B"),
                std::make_pair("Lightning Model", "IC Card Reader P2 R"),
                std::make_pair("Lightning Model", "IC Card Reader P2 G"),
                std::make_pair("Lightning Model", "IC Card Reader P2 B"),
                std::make_pair("Lightning Model", "Turntable P1 R"),
                std::make_pair("Lightning Model", "Turntable P1 G"),
                std::make_pair("Lightning Model", "Turntable P1 B"),
                std::make_pair("Lightning Model", "Turntable P2 R"),
                std::make_pair("Lightning Model", "Turntable P2 G"),
                std::make_pair("Lightning Model", "Turntable P2 B"),
                std::make_pair("Lightning Model", "Turntable P1 Resistance"),
                std::make_pair("Lightning Model", "Turntable P2 Resistance"),
                std::make_pair("Lightning Model", "Stage Left Avg R"),
                std::make_pair("Lightning Model", "Stage Left Avg G"),
                std::make_pair("Lightning Model", "Stage Left Avg B"),
                std::make_pair("Lightning Model", "Stage Right Avg R"),
                std::make_pair("Lightning Model", "Stage Right Avg G"),
                std::make_pair("Lightning Model", "Stage Right Avg B"),
                std::make_pair("Lightning Model", "Cabinet Left Avg R"),
                std::make_pair("Lightning Model", "Cabinet Left Avg G"),
                std::make_pair("Lightning Model", "Cabinet Left Avg B"),
                std::make_pair("Lightning Model", "Cabinet Right Avg R"),
                std::make_pair("Lightning Model", "Cabinet Right Avg G"),
                std::make_pair("Lightning Model", "Cabinet Right Avg B"),
                std::make_pair("Lightning Model", "Control Panel Under Avg R"),
                std::make_pair("Lightning Model", "Control Panel Under Avg G"),
                std::make_pair("Lightning Model", "Control Panel Under Avg B"),
                std::make_pair("Lightning Model", "Ceiling Left Avg R"),
                std::make_pair("Lightning Model", "Ceiling Left Avg G"),
                std::make_pair("Lightning Model", "Ceiling Left Avg B"),
                std::make_pair("Lightning Model", "Title Left Avg R"),
                std::make_pair("Lightning Model", "Title Left Avg G"),
                std::make_pair("Lightning Model", "Title Left Avg B"),
                std::make_pair("Lightning Model", "Title Right Avg R"),
                std::make_pair("Lightning Model", "Title Right Avg G"),
                std::make_pair("Lightning Model", "Title Right Avg B"),
                std::make_pair("Lightning Model", "Ceiling Right Avg R"),
                std::make_pair("Lightning Model", "Ceiling Right Avg G"),
                std::make_pair("Lightning Model", "Ceiling Right Avg B"),
                std::make_pair("Lightning Model", "Touch Panel Left Avg R"),
                std::make_pair("Lightning Model", "Touch Panel Left Avg G"),
                std::make_pair("Lightning Model", "Touch Panel Left Avg B"),
                std::make_pair("Lightning Model", "Touch Panel Right Avg R"),
                std::make_pair("Lightning Model", "Touch Panel Right Avg G"),
                std::make_pair("Lightning Model", "Touch Panel Right Avg B"),
                std::make_pair("Lightning Model", "Side Panel Left Inner Avg R"),
                std::make_pair("Lightning Model", "Side Panel Left Inner Avg G"),
                std::make_pair("Lightning Model", "Side Panel Left Inner Avg B"),
                std::make_pair("Lightning Model", "Side Panel Left Outer Avg R"),
                std::make_pair("Lightning Model", "Side Panel Left Outer Avg G"),
                std::make_pair("Lightning Model", "Side Panel Left Outer Avg B"),
                std::make_pair("Lightning Model", "Side Panel Left Avg R"),
                std::make_pair("Lightning Model", "Side Panel Left Avg G"),
                std::make_pair("Lightning Model", "Side Panel Left Avg B"),
                std::make_pair("Lightning Model", "Side Panel Right Outer Avg R"),
                std::make_pair("Lightning Model", "Side Panel Right Outer Avg G"),
                std::make_pair("Lightning Model", "Side Panel Right Outer Avg B"),
                std::make_pair("Lightning Model", "Side Panel Right Inner Avg R"),
                std::make_pair("Lightning Model", "Side Panel Right Inner Avg G"),
                std::make_pair("Lightning Model", "Side Panel Right Inner Avg B"),
                std::make_pair("Lightning Model", "Side Panel Right Avg R"),
                std::make_pair("Lightning Model", "Side Panel Right Avg G"),
                std::make_pair("Lightning Model", "Side Panel Right Avg B")
        );
    }

    return lights;
}
