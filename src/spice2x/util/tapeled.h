#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

    struct tape_led {
        std::vector<rgb_float3_t> data;
        int index_r, index_g, index_b; // Averaged RGB light output indexes
        std::string lightName;

        tape_led(size_t data_size, int index_r, int index_g, int index_b, std::string lightName)
        : data(std::vector<rgb_float3_t>(data_size)), index_r(index_r), index_g(index_g), index_b(index_b), lightName(std::move(lightName)) {}
    };

    bool is_enabled();
    rgb_float3_t pick_color_from_led_tape(uint8_t *data, size_t data_size);
    size_t get_led_index_using_avg_algo(size_t data_size);
}
