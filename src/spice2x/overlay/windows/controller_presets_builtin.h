#pragma once

#include "controller_presets.h"

namespace overlay::windows {

// PHOENIXWAN source labels
#define PWAN_P1 "Player 1"
#define PWAN_P2 "Player 2"

    static const ControllerTemplate BUILTIN_TEMPLATES[] = {

        // Beatmania IIDX - PHOENIXWAN
        {
            "PHOENIXWAN",           // name
            "Beatmania IIDX",       // game_name
            true,                   // is_builtin
            // buttons
            {
                {"P1 1",                 {0, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 2",                 {1, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 3",                 {2, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 4",                 {3, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 5",                 {4, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 6",                 {5, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 7",                 {6, BAT_NONE, PWAN_P1, false, 0, 0, 0}, {}},
                {"P1 Start",             {8, BAT_NONE, PWAN_P1, false, 0, 0, 0},
                                         {{11, BAT_NONE, PWAN_P1, false, 0, 0, 0}}},
                {"P2 1",                 {0, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 2",                 {1, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 3",                 {2, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 4",                 {3, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 5",                 {4, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 6",                 {5, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 7",                 {6, BAT_NONE, PWAN_P2, false, 0, 0, 0}, {}},
                {"P2 Start",             {8, BAT_NONE, PWAN_P2, false, 0, 0, 0},
                                         {{11, BAT_NONE, PWAN_P2, false, 0, 0, 0}}},
                {"EFFECT",               {9, BAT_NONE, PWAN_P2, false, 0, 0, 0},
                                         {{9, BAT_NONE, PWAN_P1, false, 0, 0, 0}}},
                {"VEFX",                 {10, BAT_NONE, PWAN_P2, false, 0, 0, 0},
                                         {{10, BAT_NONE, PWAN_P1, false, 0, 0, 0}}},
            },
            // keypad_buttons (none)
            {},
            // analogs
            {
                {"Turntable P1", PWAN_P1, 4, 1.f, 0.f, false, false, false, 1, false, 0},
                {"Turntable P2", PWAN_P2, 4, 1.f, 0.f, false, false, false, 1, false, 0},
            },
            // lights
            {
                {"P1 1",         {PWAN_P1, 0}, {}},
                {"P1 2",         {PWAN_P1, 1}, {}},
                {"P1 3",         {PWAN_P1, 2}, {}},
                {"P1 4",         {PWAN_P1, 3}, {}},
                {"P1 5",         {PWAN_P1, 4}, {}},
                {"P1 6",         {PWAN_P1, 5}, {}},
                {"P1 7",         {PWAN_P1, 6}, {}},
                {"P1 Start",     {PWAN_P1, 10}, {}},
    
                {"P2 1",         {PWAN_P2, 0}, {}},
                {"P2 2",         {PWAN_P2, 1}, {}},
                {"P2 3",         {PWAN_P2, 2}, {}},
                {"P2 4",         {PWAN_P2, 3}, {}},
                {"P2 5",         {PWAN_P2, 4}, {}},
                {"P2 6",         {PWAN_P2, 5}, {}},
                {"P2 7",         {PWAN_P2, 6}, {}},
                {"P2 Start",     {PWAN_P2, 7}, {}},

                {"VEFX",         {PWAN_P2, 8}, {{PWAN_P1, 8}}},
                {"Effect",       {PWAN_P2, 9}, {{PWAN_P1, 9}}},
            },
        },

    };

#undef PWAN_P1
#undef PWAN_P2

    static const int BUILTIN_TEMPLATES_COUNT =
        sizeof(BUILTIN_TEMPLATES) / sizeof(BUILTIN_TEMPLATES[0]);

}
