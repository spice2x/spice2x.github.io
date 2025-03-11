#pragma once

#include <cstdint>

namespace tapeledutils {

    enum led_tape_color_pick_algorithm {
        TAPE_LED_USE_NONE = 0,
        TAPE_LED_USE_FIRST = 1,
        TAPE_LED_USE_MIDDLE = 2,
        TAPE_LED_USE_LAST = 3,
        TAPE_LED_USE_AVERAGE = 4,
    };

    extern led_tape_color_pick_algorithm TAPE_LED_ALGORITHM;

    typedef struct {
        float r;
        float g;
        float b;
    } rgb_float3_t;

    bool is_enabled();
    rgb_float3_t pick_color_from_led_tape(uint8_t *data, size_t data_size);
    size_t get_led_index_using_avg_algo(size_t data_size);
}
