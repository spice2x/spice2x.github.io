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

        using namespace GameAPI::Analogs;

        GameAPI::Analogs::sortAnalogsWithType(&analogs, {
            { "Turntable P1", AnalogType::Circular },
            { "Turntable P2", AnalogType::Circular },
            { "VEFX", AnalogType::LinearPositive },
            { "Low-EQ", AnalogType::LinearPositive },
            { "Hi-EQ", AnalogType::LinearPositive },
            { "Filter", AnalogType::LinearPositive },
            { "Play Volume", AnalogType::LinearPositive }
        });
    }
    return analogs;
}

std::vector<Light> &games::iidx::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("Beatmania IIDX");

        GameAPI::Lights::sortLightsWithCategory(
                &lights,
                {
                    {"Common", "P1 1"},
                    {"Common", "P1 2"},
                    {"Common", "P1 3"},
                    {"Common", "P1 4"},
                    {"Common", "P1 5"},
                    {"Common", "P1 6"},
                    {"Common", "P1 7"},
                    {"Common", "P2 1"},
                    {"Common", "P2 2"},
                    {"Common", "P2 3"},
                    {"Common", "P2 4"},
                    {"Common", "P2 5"},
                    {"Common", "P2 6"},
                    {"Common", "P2 7"},
                    {"Common", "P1 Start"},
                    {"Common", "P2 Start"},
                    {"Common", "VEFX"},
                    {"Common", "Effect"},
                    {"DX", "Spot Light 1"},
                    {"DX", "Spot Light 2"},
                    {"DX", "Spot Light 3"},
                    {"DX", "Spot Light 4"},
                    {"DX", "Spot Light 5"},
                    {"DX", "Spot Light 6"},
                    {"DX", "Spot Light 7"},
                    {"DX", "Spot Light 8"},
                    {"DX", "Neon Lamp"},
                    {"Lightning Model", "Woofer R"},
                    {"Lightning Model", "Woofer G"},
                    {"Lightning Model", "Woofer B"},
                    {"Lightning Model", "IC Card Reader P1 R"},
                    {"Lightning Model", "IC Card Reader P1 G"},
                    {"Lightning Model", "IC Card Reader P1 B"},
                    {"Lightning Model", "IC Card Reader P2 R"},
                    {"Lightning Model", "IC Card Reader P2 G"},
                    {"Lightning Model", "IC Card Reader P2 B"},
                    {"Lightning Model", "Turntable P1 R"},
                    {"Lightning Model", "Turntable P1 G"},
                    {"Lightning Model", "Turntable P1 B"},
                    {"Lightning Model", "Turntable P2 R"},
                    {"Lightning Model", "Turntable P2 G"},
                    {"Lightning Model", "Turntable P2 B"},
                    {"Lightning Model", "Turntable P1 Resistance"},
                    {"Lightning Model", "Turntable P2 Resistance"},
                    {"Lightning Model", "Stage Left Avg R"},
                    {"Lightning Model", "Stage Left Avg G"},
                    {"Lightning Model", "Stage Left Avg B"},
                    {"Lightning Model", "Stage Right Avg R"},
                    {"Lightning Model", "Stage Right Avg G"},
                    {"Lightning Model", "Stage Right Avg B"},
                    {"Lightning Model", "Cabinet Left Avg R"},
                    {"Lightning Model", "Cabinet Left Avg G"},
                    {"Lightning Model", "Cabinet Left Avg B"},
                    {"Lightning Model", "Cabinet Right Avg R"},
                    {"Lightning Model", "Cabinet Right Avg G"},
                    {"Lightning Model", "Cabinet Right Avg B"},
                    {"Lightning Model", "Control Panel Under Avg R"},
                    {"Lightning Model", "Control Panel Under Avg G"},
                    {"Lightning Model", "Control Panel Under Avg B"},
                    {"Lightning Model", "Ceiling Left Avg R"},
                    {"Lightning Model", "Ceiling Left Avg G"},
                    {"Lightning Model", "Ceiling Left Avg B"},
                    {"Lightning Model", "Title Left Avg R"},
                    {"Lightning Model", "Title Left Avg G"},
                    {"Lightning Model", "Title Left Avg B"},
                    {"Lightning Model", "Title Right Avg R"},
                    {"Lightning Model", "Title Right Avg G"},
                    {"Lightning Model", "Title Right Avg B"},
                    {"Lightning Model", "Ceiling Right Avg R"},
                    {"Lightning Model", "Ceiling Right Avg G"},
                    {"Lightning Model", "Ceiling Right Avg B"},
                    {"Lightning Model", "Touch Panel Left Avg R"},
                    {"Lightning Model", "Touch Panel Left Avg G"},
                    {"Lightning Model", "Touch Panel Left Avg B"},
                    {"Lightning Model", "Touch Panel Right Avg R"},
                    {"Lightning Model", "Touch Panel Right Avg G"},
                    {"Lightning Model", "Touch Panel Right Avg B"},
                    {"Lightning Model", "Side Panel Left Inner Avg R"},
                    {"Lightning Model", "Side Panel Left Inner Avg G"},
                    {"Lightning Model", "Side Panel Left Inner Avg B"},
                    {"Lightning Model", "Side Panel Left Outer Avg R"},
                    {"Lightning Model", "Side Panel Left Outer Avg G"},
                    {"Lightning Model", "Side Panel Left Outer Avg B"},
                    {"Lightning Model", "Side Panel Left Avg R"},
                    {"Lightning Model", "Side Panel Left Avg G"},
                    {"Lightning Model", "Side Panel Left Avg B"},
                    {"Lightning Model", "Side Panel Right Outer Avg R"},
                    {"Lightning Model", "Side Panel Right Outer Avg G"},
                    {"Lightning Model", "Side Panel Right Outer Avg B"},
                    {"Lightning Model", "Side Panel Right Inner Avg R"},
                    {"Lightning Model", "Side Panel Right Inner Avg G"},
                    {"Lightning Model", "Side Panel Right Inner Avg B"},
                    {"Lightning Model", "Side Panel Right Avg R"},
                    {"Lightning Model", "Side Panel Right Avg G"},
                    {"Lightning Model", "Side Panel Right Avg B"}
                }
        );
    }

    return lights;
}
