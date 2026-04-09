#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>

#include <windows.h>

namespace xinput {

    #define XUSER_MAX_COUNT 4

    typedef struct _XINPUT_GAMEPAD_STATE {
        uint16_t wButtons;
        uint8_t bLeftTrigger;
        uint8_t bRightTrigger;
        int16_t sThumbLX;
        int16_t sThumbLY;
        int16_t sThumbRX;
        int16_t sThumbRY;
    } XINPUT_GAMEPAD_STATE;

    typedef struct _XINPUT_GAMEPAD_STATE_NORMALIZED {
        uint16_t wButtons;
        float bLeftTrigger;
        float bRightTrigger;
        float sThumbLX;
        float sThumbLY;
        float sThumbRX;
        float sThumbRY;
    } XINPUT_GAMEPAD_STATE_NORMALIZED;

    enum class XInputButtonEnum {
        // actual buttons
        DPAD_UP,
        DPAD_DOWN,
        DPAD_LEFT,
        DPAD_RIGHT,
        START,
        BACK,
        LEFT_STICK,
        RIGHT_STICK,
        LEFT_SHOULDER,
        RIGHT_SHOULDER,
        BUTTON_A,
        BUTTON_B,
        BUTTON_X,
        BUTTON_Y,

        // analog values that can be used as buttons
        LEFT_TRIGGER,
        RIGHT_TRIGGER,
        LEFT_STICK_UP,
        LEFT_STICK_DOWN,
        LEFT_STICK_LEFT,
        LEFT_STICK_RIGHT,
        RIGHT_STICK_UP,
        RIGHT_STICK_DOWN,
        RIGHT_STICK_LEFT,
        RIGHT_STICK_RIGHT,

        COUNT
    };

    enum class XInputAnalogEnum {
        // analog values
        LEFT_TRIGGER,
        RIGHT_TRIGGER,
        LEFT_STICK_X,
        LEFT_STICK_Y,
        RIGHT_STICK_X,
        RIGHT_STICK_Y,

        COUNT
    };

    std::string get_button_string(XInputButtonEnum button);
    std::string get_analog_string(XInputAnalogEnum analog);

    class XInputManager {
    private:
        bool initialized = false;
        HMODULE xinput_lib = nullptr;
        void get_state(uint8_t player, XINPUT_GAMEPAD_STATE_NORMALIZED *state);
    public:
        XInputManager();
        ~XInputManager();
        void stop();
        std::vector<uint8_t> get_available_players();
        bool is_button_pressed(uint8_t player, XInputButtonEnum button);
        float get_analog_state(uint8_t player, XInputAnalogEnum analog);
    };
}
