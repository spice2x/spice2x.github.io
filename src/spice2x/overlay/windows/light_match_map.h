#pragma once

#include <cstdlib>

namespace overlay::windows {

    struct LightMatchMap {
        const char *controller;     // controller name for display
        const char *game;           // game name to match
        unsigned short vid;         // USB vendor ID filter, or 0 to skip
        unsigned short pid;         // USB product ID filter, or 0 to skip
        const char *device_light;   // light name (or prefix if rgb)
        const char *device_suffix;  // after R/G/B for rgb mode, or ""
        const char *address;        // control index for address mode, or ""
        bool rgb;                   // expand " R/G/B" between device_light and device_suffix
        const char *game_light;     // target game light (or prefix if rgb), without P1/P2 prefix
        const char *game_light_alt; // alt game light for Valkyrie mode, or ""
    };

    static const LightMatchMap LIGHT_MATCH_MAP[] = {

        // FAUCETWO - Sound Voltex (RGB)
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 6/24", "", true, "Wing Left Up", "Upper Left Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 12/24", "", true, "Wing Left Low", "Lower Left Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 18/24", "", true, "Wing Right Low", "Lower Right Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Top LED", " 24/24", "", true, "Wing Right Up", "Upper Right Speaker Avg"},
        {"FAUCETWO", "Sound Voltex", 0, 0, "Left and Right", "", "", true, "Controller", "Control Panel Avg"},

        // FAUCETWO Portable - Sound Voltex (RGB)
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Left Side", "", "", true, "Wing Left Up", "Left Wing Avg"},
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Right Side", "", "", true, "Wing Right Up", "Right Wing Avg"},
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Center1", "", "", true, "Controller", "Control Panel Avg"},
        {"FAUCETWO Portable", "Sound Voltex", 0, 0, "Light of Center2", "", "", true, "Woofer", "Woofer Avg"},

        // PHOENIXWAN - Beatmania IIDX (address based, VID:PID 034C:0368)
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x00", false, "1", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x01", false, "2", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x02", false, "3", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x03", false, "4", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x04", false, "5", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x05", false, "6", ""},
        {"PHOENIXWAN", "Beatmania IIDX", 0x034C, 0x0368, "Generic Indicator", "", "0x06", false, "7", ""},

        // Nostroller VID_0E8F&PID_1212&MI_03 (Lights 01-14) (RGB)
        // The lights are named "LED_01R" "LED_01G" "LED_01B" and so on.
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_01", "", "", true, "Key 1", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_02", "", "", true, "Key 2", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_03", "", "", true, "Key 3", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_04", "", "", true, "Key 4", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_05", "", "", true, "Key 5", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_06", "", "", true, "Key 6", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_07", "", "", true, "Key 7", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_08", "", "", true, "Key 8", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_09", "", "", true, "Key 9", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_10", "", "", true, "Key 10", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_11", "", "", true, "Key 11", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_12", "", "", true, "Key 12", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_13", "", "", true, "Key 13", ""},
        {"Nostroller (Lights 01-14)", "Nostalgia", 0, 0, "LED_14", "", "", true, "Key 14", ""},

        // Nostroller VID_0E8F&PID_1212&MI_02 (Lights 15-28) (RGB)
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_15", "", "", true, "Key 15", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_16", "", "", true, "Key 16", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_17", "", "", true, "Key 17", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_18", "", "", true, "Key 18", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_19", "", "", true, "Key 19", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_20", "", "", true, "Key 20", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_21", "", "", true, "Key 21", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_22", "", "", true, "Key 22", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_23", "", "", true, "Key 23", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_24", "", "", true, "Key 24", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_25", "", "", true, "Key 25", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_26", "", "", true, "Key 26", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_27", "", "", true, "Key 27", ""},
        {"Nostroller (Lights 15-28)", "Nostalgia", 0, 0, "LED_28", "", "", true, "Key 28", ""},

        // icedragon.io snek board, ddr mode
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "neon", "", "", false, "Neon", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "mar p1 upper", "", "", false, "P1 Halogen Upper", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "mar p1 lower", "", "", false, "P1 Halogen Lower", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "mar p2 upper", "", "", false, "P2 Halogen Upper", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "mar p2 lower", "", "", false, "P2 Halogen Lower", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p1 buttons", "", "", false, "P1 Button", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p2 buttons", "", "", false, "P2 Button", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p1 up", "", "", false, "P1 Foot Up", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p1 down", "", "", false, "P1 Foot Down", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p1 left", "", "", false, "P1 Foot Left", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p1 right", "", "", false, "P1 Foot Right", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p2 up", "", "", false, "P2 Foot Up", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p2 down", "", "", false, "P2 Foot Down", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p2 left", "", "", false, "P2 Foot Left", ""},
        {"snek lights", "Dance Dance Revolution", 0x2e8a, 0x10a8, "p2 right", "", "", false, "P2 Foot Right", ""},

    };

    static const int LIGHT_MATCH_MAP_COUNT =
        sizeof(LIGHT_MATCH_MAP) / sizeof(LIGHT_MATCH_MAP[0]);

}
