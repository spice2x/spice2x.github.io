#include "io.h"

std::vector<Button> &games::gitadora::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("GitaDora");

        GameAPI::Buttons::sortButtons(&buttons,
                "Service",
                "Test",
                "Coin",
                "Headphone",
                "Guitar P1 Start",
                "Guitar P1 Up",
                "Guitar P1 Down",
                "Guitar P1 Left",
                "Guitar P1 Right",
                "Guitar P1 Help",
                "Guitar P1 Effect 1",
                "Guitar P1 Effect 2",
                "Guitar P1 Effect 3",
                "Guitar P1 Effect Pedal",
                "Guitar P1 Button Extra 1",
                "Guitar P1 Button Extra 2",
                "Guitar P1 Pick Up",
                "Guitar P1 Pick Down",
                "Guitar P1 R",
                "Guitar P1 G",
                "Guitar P1 B",
                "Guitar P1 Y",
                "Guitar P1 P",
                "Guitar P1 Knob Up",
                "Guitar P1 Knob Down",
                "Guitar P1 Wail Up",
                "Guitar P1 Wail Down",
                "Guitar P2 Start",
                "Guitar P2 Up",
                "Guitar P2 Down",
                "Guitar P2 Left",
                "Guitar P2 Right",
                "Guitar P2 Help",
                "Guitar P2 Effect 1",
                "Guitar P2 Effect 2",
                "Guitar P2 Effect 3",
                "Guitar P2 Effect Pedal",
                "Guitar P2 Button Extra 1",
                "Guitar P2 Button Extra 2",
                "Guitar P2 Pick Up",
                "Guitar P2 Pick Down",
                "Guitar P2 R",
                "Guitar P2 G",
                "Guitar P2 B",
                "Guitar P2 Y",
                "Guitar P2 P",
                "Guitar P2 Knob Up",
                "Guitar P2 Knob Down",
                "Guitar P2 Wail Up",
                "Guitar P2 Wail Down",
                "Drum Start",
                "Drum Up",
                "Drum Down",
                "Drum Left",
                "Drum Right",
                "Drum Help",
                "Drum Button Extra 1",
                "Drum Button Extra 2",

                "Drum Left Cymbal",
                "Drum Hi-Hat",
                "Drum Left Pedal",
                "Drum Snare",
                "Drum Hi-Tom",
                "Drum Bass Pedal",
                "Drum Low-Tom",
                "Drum Floor Tom",
                "Drum Right Cymbal",
                "Drum Hi-Hat Closed",
                "Drum Hi-Hat Half-Open"
        );
    }

    return buttons;
}

std::vector<Analog> &games::gitadora::get_analogs() {
    static std::vector<Analog> analogs;

    if (analogs.empty()) {
        analogs = GameAPI::Analogs::getAnalogs("GitaDora");

        GameAPI::Analogs::sortAnalogs(&analogs,
                "Guitar P1 Wail X",
                "Guitar P1 Wail Y",
                "Guitar P1 Wail Z",
                "Guitar P1 Knob",
                "Guitar P2 Wail X",
                "Guitar P2 Wail Y",
                "Guitar P2 Wail Z",
                "Guitar P2 Knob"
        );
    }

    return analogs;
}

std::string games::gitadora::get_buttons_help() {
    // keep to max 100 characters wide
    return
        "guitar:\n"
        "\n"
        " < R G B Y P --- Pick ] \n"
        "\n"
        " If you normally hold your guitar left-handed (frets on right hand):\n"
        "   * here, hold the guitar as right-handed (frets on left hand),\n"
        "     and bind the buttons, pick, and wail controls\n"
        "   * in Options tab, turn on GitaDora Lefty Guitar\n"
        "   * finally, use the in-game option to turn on LEFT mode\n"
        "\n"
        "\n"
        "drums:\n"
        "\n"
        " LeftCymbal                                RightCymbal\n"
        "         HiHat        HiTom      LowTom \n"
        "                  Snare              FloorTom \n"
        " -------------------------------------------------------\n"
        "             Left          Bass\n"
        "             Pedal         Pedal\n"
        "\n"
        " For MIDI drums with Open/Closed HiHat configurations or pads with\n"
        " multiple hit zones, ensure you bind all variation using the Pages\n"
        " button at the bottom."
        ;
}
std::string games::gitadora::get_analogs_help() {
    // keep to max 100 characters wide
    return
        "guitar:\n"
        "\n"
        "X axis: 0% when body is held facing monitor, 50% when flat on a table\n"
        "Y axis: 50% when held horizontal, 0% when neck is raised, 100% pointing down\n"
        "Z axis: 50% at rest, 100% when swinging neck toward monitor (only used in XG series)\n"
        "\n"
        "You need both X and Y axis for up/down wail to work correctly in-game.\n"
        "If you only have Y axis, consider using digital wailing instead.\n"
        "\n"
        "Ensure you clear all analog bindings if you want to use digital wailing.\n"
        "\n"
        "If you hold your guitar left-handed (frets on the right hand),\n"
        "analog bindings will likely not work as intended.\n"
        "Consider using digital wailing in Buttons tab."
        ;
}

std::vector<Light> &games::gitadora::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("GitaDora");

        GameAPI::Lights::sortLightsWithCategory(&lights,
            {
                {"Guitar", "Guitar P1 Motor"},
                {"Guitar", "Guitar P2 Motor"},

                {"Menu", "P1 Start"},
                {"Menu", "P1 Menu Up Down (DX)"},
                {"Menu", "P1 Menu Left Right (DX)"},
                {"Menu", "P1 Help (DX)"},

                {"Menu", "P2 Start"},
                {"Menu", "P2 Menu Up Down (DX)"},
                {"Menu", "P2 Menu Left Right (DX)"},
                {"Menu", "P2 Help (DX)"},

                {"Drums", "Drum Left Cymbal"},
                {"Drums", "Drum Hi-Hat"},
                {"Drums", "Drum Snare"},
                {"Drums", "Drum High Tom"},
                {"Drums", "Drum Low Tom"},
                {"Drums", "Drum Floor Tom"},
                {"Drums", "Drum Right Cymbal"},

                {"DX/SD", "Drum Woofer R"},
                {"DX/SD", "Drum Woofer G"},
                {"DX/SD", "Drum Woofer B"},

                {"DX/SD", "Drum Stage R (DX)"},
                {"DX/SD", "Drum Stage G (DX)"},
                {"DX/SD", "Drum Stage B (DX)"},

                {"DX/SD", "Spot Left (DX)"},
                {"DX/SD", "Spot Right (DX)"},
                {"DX/SD", "Spot Center Left (DX)"},
                {"DX/SD", "Spot Center Right (DX)"},

                {"DX/SD", "Drum Spot Rear Left (DX)"},
                {"DX/SD", "Drum Spot Rear Right (DX)"},

                {"DX/SD", "Guitar Lower Left R (DX)"},
                {"DX/SD", "Guitar Lower Left G (DX)"},
                {"DX/SD", "Guitar Lower Left B (DX)"},
                {"DX/SD", "Guitar Lower Right R (DX)"},
                {"DX/SD", "Guitar Lower Right G (DX)"},
                {"DX/SD", "Guitar Lower Right B (DX)"},

                {"DX/SD", "Guitar Left Speaker Upper R (DX)"},
                {"DX/SD", "Guitar Left Speaker Upper G (DX)"},
                {"DX/SD", "Guitar Left Speaker Upper B (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Up Left R (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Up Left G (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Up Left B (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Up Right R (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Up Right G (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Up Right B (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Low Left R (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Low Left G (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Low Left B (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Low Right R (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Low Right G (DX)"},
                {"DX/SD", "Guitar Left Speaker Mid Low Right B (DX)"},
                {"DX/SD", "Guitar Left Speaker Lower R (DX)"},
                {"DX/SD", "Guitar Left Speaker Lower G (DX)"},
                {"DX/SD", "Guitar Left Speaker Lower B (DX)"},

                {"DX/SD", "Guitar Right Speaker Upper R (DX)"},
                {"DX/SD", "Guitar Right Speaker Upper G (DX)"},
                {"DX/SD", "Guitar Right Speaker Upper B (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Up Left R (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Up Left G (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Up Left B (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Up Right R (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Up Right G (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Up Right B (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Low Left R (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Low Left G (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Low Left B (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Low Right R (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Low Right G (DX)"},
                {"DX/SD", "Guitar Right Speaker Mid Low Right B (DX)"},
                {"DX/SD", "Guitar Right Speaker Lower R (DX)"},
                {"DX/SD", "Guitar Right Speaker Lower G (DX)"},
                {"DX/SD", "Guitar Right Speaker Lower B (DX)"},

                {"Arena Model", "Title Average R (Arena)"},
                {"Arena Model", "Title Average G (Arena)"},
                {"Arena Model", "Title Average B (Arena)"},

                {"Arena Model", "Woofer R (Arena)"},
                {"Arena Model", "Woofer G (Arena)"},
                {"Arena Model", "Woofer B (Arena)"},

                {"Arena Model", "Card Reader R (Arena)"},
                {"Arena Model", "Card Reader G (Arena)"},
                {"Arena Model", "Card Reader B (Arena)"}
            }
        );
    }

    return lights;
}
