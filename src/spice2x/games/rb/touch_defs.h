#pragma once

namespace games::rb {
    inline constexpr int TOUCH_SCALE_DEFAULT = 1000;
    inline constexpr int TOUCH_PACKET_SIZE = 20;
    inline constexpr int TOUCH_PACKET_DATA_OFFSET = 3;

    inline constexpr int X_SENSOR_COUNT = 48;
    inline constexpr int X_SENSOR_FIRST_ACTIVE = 2;
    inline constexpr int X_SENSOR_LAST_ACTIVE = 45;
    inline constexpr int X_SENSOR_ACTIVE_COUNT =
        X_SENSOR_LAST_ACTIVE - X_SENSOR_FIRST_ACTIVE + 1;
    inline constexpr int X_SENSOR_FIRST_BIT = 88;

    inline constexpr int Y_SENSOR_COUNT = 76;
    inline constexpr int Y_SENSOR_FIRST_BIT = 75;
}
