#include "io.h"

std::vector<Button> &games::gitadora::get_buttons() {
    static std::vector<Button> buttons;

    if (buttons.empty()) {
        buttons = GameAPI::Buttons::getButtons("GitaDora");

        GameAPI::Buttons::sortButtons(&buttons,
                "Service",
                "Test",
                "Coin",
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
                "Drum Hi-Hat",
                "Drum Hi-Hat Closed",
                "Drum Hi-Hat Half-Open",
                "Drum Snare",
                "Drum Hi-Tom",
                "Drum Low-Tom",
                "Drum Right Cymbal",
                "Drum Bass Pedal",
                "Drum Left Cymbal",
                "Drum Left Pedal",
                "Drum Floor Tom"
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
        " < R G B Y P --- Pick ] \n"
        "\n"
        "drums:\n"
        " LeftCymbal                                RightCymbal\n"
        "         HiHat        HiTom      LowTom \n"
        "                  Snare              FloorTom \n"
        " -------------------------------------------------------\n"
        "             Left          Bass\n"
        "             Pedal         Pedal\n"
        "\n"
        "Drums are NOT velocity-sensitive!\n"
        "\n"
        "For MIDI drums with Open/Closed HiHat configurations, bind variations below. "
        "v2_drum algorithm might work better for those drums."
        ;
}

std::vector<Light> &games::gitadora::get_lights() {
    static std::vector<Light> lights;

    if (lights.empty()) {
        lights = GameAPI::Lights::getLights("GitaDora");

        GameAPI::Lights::sortLights(&lights,
                "Guitar P1 Motor",
                "Guitar P2 Motor",

                "P1 Start",
                "P1 Menu Up Down (DX)",
                "P1 Menu Left Right (DX)",
                "P1 Help (DX)",

                "P2 Start",
                "P2 Menu Up Down (DX)",
                "P2 Menu Left Right (DX)",
                "P2 Help (DX)",

                "Drum Left Cymbal",
                "Drum Hi-Hat",
                "Drum Snare",
                "Drum High Tom",
                "Drum Low Tom",
                "Drum Floor Tom",
                "Drum Right Cymbal",

                "Drum Woofer R",
                "Drum Woofer G",
                "Drum Woofer B",

                "Drum Stage R (DX)",
                "Drum Stage G (DX)",
                "Drum Stage B (DX)",

                "Spot Left (DX)",
                "Spot Right (DX)",
                "Spot Center Left (DX)",
                "Spot Center Right (DX)",

                "Drum Spot Rear Left (DX)",
                "Drum Spot Rear Right (DX)",

                "Guitar Lower Left R (DX)",
                "Guitar Lower Left G (DX)",
                "Guitar Lower Left B (DX)",
                "Guitar Lower Right R (DX)",
                "Guitar Lower Right G (DX)",
                "Guitar Lower Right B (DX)",

                "Guitar Left Speaker Upper R (DX)",
                "Guitar Left Speaker Upper G (DX)",
                "Guitar Left Speaker Upper B (DX)",
                "Guitar Left Speaker Mid Up Left R (DX)",
                "Guitar Left Speaker Mid Up Left G (DX)",
                "Guitar Left Speaker Mid Up Left B (DX)",
                "Guitar Left Speaker Mid Up Right R (DX)",
                "Guitar Left Speaker Mid Up Right G (DX)",
                "Guitar Left Speaker Mid Up Right B (DX)",
                "Guitar Left Speaker Mid Low Left R (DX)",
                "Guitar Left Speaker Mid Low Left G (DX)",
                "Guitar Left Speaker Mid Low Left B (DX)",
                "Guitar Left Speaker Mid Low Right R (DX)",
                "Guitar Left Speaker Mid Low Right G (DX)",
                "Guitar Left Speaker Mid Low Right B (DX)",
                "Guitar Left Speaker Lower R (DX)",
                "Guitar Left Speaker Lower G (DX)",
                "Guitar Left Speaker Lower B (DX)",

                "Guitar Right Speaker Upper R (DX)",
                "Guitar Right Speaker Upper G (DX)",
                "Guitar Right Speaker Upper B (DX)",
                "Guitar Right Speaker Mid Up Left R (DX)",
                "Guitar Right Speaker Mid Up Left G (DX)",
                "Guitar Right Speaker Mid Up Left B (DX)",
                "Guitar Right Speaker Mid Up Right R (DX)",
                "Guitar Right Speaker Mid Up Right G (DX)",
                "Guitar Right Speaker Mid Up Right B (DX)",
                "Guitar Right Speaker Mid Low Left R (DX)",
                "Guitar Right Speaker Mid Low Left G (DX)",
                "Guitar Right Speaker Mid Low Left B (DX)",
                "Guitar Right Speaker Mid Low Right R (DX)",
                "Guitar Right Speaker Mid Low Right G (DX)",
                "Guitar Right Speaker Mid Low Right B (DX)",
                "Guitar Right Speaker Lower R (DX)",
                "Guitar Right Speaker Lower G (DX)",
                "Guitar Right Speaker Lower B (DX)"
        );
    }

    return lights;
}
