#include "rawinput/xinput.h"
#include "util/logging.h"

namespace xinput {

// this is all we need to emulate xinput.h which we avoid including here...

#define XINPUT_GAMEPAD_TRIGGER_THRESHOLD (30 / 255.0f)
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849
#define XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE 8689

#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

typedef struct {
    uint32_t dwPacketNumber;
    XINPUT_GAMEPAD_STATE Gamepad;
} XINPUT_STATE;

DWORD
WINAPI
XInputGetState(
    DWORD dwUserIndex,
    XINPUT_STATE *pState
);

typedef struct {
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
} XINPUT_VIBRATION;

DWORD
WINAPI
XInputSetState(
    DWORD dwUserIndex,
    XINPUT_VIBRATION* pVibration
);

// end xinput definitions

    std::string get_button_string(XInputButtonEnum button) {
        switch (button) {
            case XInputButtonEnum::DPAD_UP:
                return "Dpad Up";
            case XInputButtonEnum::DPAD_DOWN:
                return "Dpad Down";
            case XInputButtonEnum::DPAD_LEFT:
                return "Dpad Left";
            case XInputButtonEnum::DPAD_RIGHT:
                return "Dpad Right";
            case XInputButtonEnum::START:
                return "Start";
            case XInputButtonEnum::BACK:
                return "Back";
            case XInputButtonEnum::LEFT_STICK:
                return "Left Stick Click";
            case XInputButtonEnum::RIGHT_STICK:
                return "Right Stick Click";
            case XInputButtonEnum::LEFT_SHOULDER:
                return "Left Bumper";
            case XInputButtonEnum::RIGHT_SHOULDER:
                return "Right Bumper";
            case XInputButtonEnum::BUTTON_A:
                return "Button A";
            case XInputButtonEnum::BUTTON_B:
                return "Button B";
            case XInputButtonEnum::BUTTON_X:
                return "Button X";
            case XInputButtonEnum::BUTTON_Y:
                return "Button Y";
            case XInputButtonEnum::LEFT_TRIGGER:
                return "Left Trigger";
            case XInputButtonEnum::RIGHT_TRIGGER:
                return "Right Trigger";
            case XInputButtonEnum::LEFT_STICK_UP:
                return "Left Stick, Up";
            case XInputButtonEnum::LEFT_STICK_DOWN:
                return "Left Stick, Down";
            case XInputButtonEnum::LEFT_STICK_LEFT:
                return "Left Stick, Left";
            case XInputButtonEnum::LEFT_STICK_RIGHT:
                return "Left Stick, Right";
            case XInputButtonEnum::RIGHT_STICK_UP:
                return "Right Stick, Up";
            case XInputButtonEnum::RIGHT_STICK_DOWN:
                return "Right Stick, Down";
            case XInputButtonEnum::RIGHT_STICK_LEFT:
                return "Right Stick, Left";
            case XInputButtonEnum::RIGHT_STICK_RIGHT:
                return "Right Stick, Right";
            default:
                break;
        }
        return fmt::format("Unknown Button ({})", static_cast<int>(button));
    }

    std::string get_analog_string(XInputAnalogEnum analog) {
        switch (analog) {
            case XInputAnalogEnum::LEFT_TRIGGER:
                return "Left Trigger";
            case XInputAnalogEnum::RIGHT_TRIGGER:
                return "Right Trigger";
            case XInputAnalogEnum::LEFT_STICK_X:
                return "Left Stick X";
            case XInputAnalogEnum::LEFT_STICK_Y:
                return "Left Stick Y";
            case XInputAnalogEnum::RIGHT_STICK_X:
                return "Right Stick X";
            case XInputAnalogEnum::RIGHT_STICK_Y:
                return "Right Stick Y";
            default:
                break;
        }
        return fmt::format("Unknown Analog ({})", static_cast<int>(analog));
    }
    
    std::string get_output_string(XInputOutputEnum output) {
        switch (output) {
            case XInputOutputEnum::LEFT_RUMBLE:
                return "Left Rumble";
            case XInputOutputEnum::RIGHT_RUMBLE:
                return "Right Rumble";
            default:
                break;
        }
        return fmt::format("Unknown Output ({})", static_cast<int>(output));
    }

    std::string get_device_desc(uint8_t player) {
        return fmt::format(";XINPUT;{}", player);
    }

#if defined(SPICE_XP)

    XInputManager::XInputManager() {}
    XInputManager::~XInputManager() {}
    void XInputManager::stop() {}
    std::vector<uint8_t> XInputManager::get_available_players() {
        return {};
    }
    float XInputManager::get_analog_state(uint8_t player, XInputAnalogEnum analog) {
        return 0.5f;
    }
    bool XInputManager::is_button_pressed(uint8_t player, XInputButtonEnum button) {
        return false;
    }
    bool XInputManager::get_any_button_pressed(XINPUT_NEW_BUTTON &button) {
        return false;
    }
    void XInputManager::set_output_state(uint8_t player, XInputOutputEnum output, float value) {
        return;
    }

#else

    static decltype(XInputGetState) *XInputGetState_addr = nullptr;
    static decltype(XInputSetState) *XInputSetState_addr = nullptr;

    XInputManager::XInputManager() {
        log_info("xinput", "initialize...");
        this->xinput_lib = LoadLibraryA("xinput1_3.dll");
        if (!this->xinput_lib) {
            log_warning("xinput", "failed to load xinput1_3.dll");
            return;
        }
        XInputGetState_addr = reinterpret_cast<decltype(XInputGetState) *>(
            GetProcAddress(this->xinput_lib, "XInputGetState"));
        if (!XInputGetState_addr) {
            log_warning("xinput", "failed to get XInputGetState address");
            this->stop();
            return;
        }
        XInputSetState_addr = reinterpret_cast<decltype(XInputSetState) *>(
            GetProcAddress(this->xinput_lib, "XInputSetState"));
        if (!XInputSetState_addr) {
            log_warning("xinput", "failed to get XInputSetState address");
            this->stop();
            return;
        }
        initialized = true;
        log_info("xinput", "initialized");
    }

    XInputManager::~XInputManager() {
        this->stop();
    }

    void XInputManager::stop() {
        this->initialized = false;
        if (this->xinput_lib) {
            FreeLibrary(this->xinput_lib);
            this->xinput_lib = nullptr;
        }
        XInputGetState_addr = nullptr;
        XInputSetState_addr = nullptr;
        log_info("xinput", "destroyed");
    }

    std::vector<uint8_t> XInputManager::get_available_players() {
        std::vector<uint8_t> players;
        if (!this->initialized) {
            return players;
        }
        
        for (uint8_t i = 0; i < XUSER_MAX_COUNT; i++) {
            XINPUT_STATE x;
            if (XInputGetState_addr(i, &x) == ERROR_SUCCESS) {
                players.push_back(i);
            }
        }
        return players;
    }

    void XInputManager::get_state(uint8_t player, XINPUT_GAMEPAD_STATE_NORMALIZED &state) {
        state = {};
        state.sThumbLX = 0.5f;
        state.sThumbLY = 0.5f;
        state.sThumbRX = 0.5f;
        state.sThumbRY = 0.5f;
        if (!this->initialized) {
            return;
        }
        
        XINPUT_STATE x;
        if (XInputGetState_addr(player, &x) != ERROR_SUCCESS) {
            return;
        }

        state.wButtons = x.Gamepad.wButtons;
        state.bLeftTrigger = x.Gamepad.bLeftTrigger / 255.0f;
        state.bRightTrigger = x.Gamepad.bRightTrigger / 255.0f;

        // left stick circular dead zone
        {
            const auto x_raw = x.Gamepad.sThumbLX;
            const auto y_raw = -x.Gamepad.sThumbLY; // flip to make [down = positive]
            const float magnitude = sqrtf(x_raw * x_raw + y_raw * y_raw);
            if (magnitude > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
                const float scaled =
                    (magnitude - XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) /
                    (32767.0f - XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

                state.sThumbLX = (x_raw / magnitude) * scaled;
                state.sThumbLY = (y_raw / magnitude) * scaled;

                // normalize to [0, 1]
                state.sThumbLX = std::clamp((state.sThumbLX + 1.f) / 2.f, 0.f, 1.f);
                state.sThumbLY = std::clamp((state.sThumbLY + 1.f) / 2.f, 0.f, 1.f);
            } else {
                // within deadzone
                state.sThumbLX = 0.5f;
                state.sThumbLY = 0.5f;
            }
        }

        // right stick circular dead zone
        {
            const auto x_raw = x.Gamepad.sThumbRX;
            const auto y_raw = -x.Gamepad.sThumbRY; // flip to make [down = positive]
            const float magnitude = sqrtf(x_raw * x_raw + y_raw * y_raw);
            if (magnitude > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
                const float scaled =
                    (magnitude - XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) /
                    (32767.0f - XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

                state.sThumbRX = (x_raw / magnitude) * scaled;
                state.sThumbRY = (y_raw / magnitude) * scaled;

                // normalize to [0, 1]
                state.sThumbRX = std::clamp((state.sThumbRX + 1.f) / 2.f, 0.f, 1.f);
                state.sThumbRY = std::clamp((state.sThumbRY + 1.f) / 2.f, 0.f, 1.f);
            } else {
                // within deadzone
                state.sThumbRX = 0.5f;
                state.sThumbRY = 0.5f;
            }
        }
    }

    bool XInputManager::is_button_pressed(uint8_t player, XInputButtonEnum button) {
        XINPUT_GAMEPAD_STATE_NORMALIZED state;
    
        get_state(player, state);
        switch (button) {
            case XInputButtonEnum::DPAD_UP:
                return (state.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            case XInputButtonEnum::DPAD_DOWN:
                return (state.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
            case XInputButtonEnum::DPAD_LEFT:
                return (state.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
            case XInputButtonEnum::DPAD_RIGHT:
                return (state.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
            case XInputButtonEnum::START:
                return (state.wButtons & XINPUT_GAMEPAD_START) != 0;
            case XInputButtonEnum::BACK:
                return (state.wButtons & XINPUT_GAMEPAD_BACK) != 0;
            case XInputButtonEnum::LEFT_STICK:
                return (state.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
            case XInputButtonEnum::RIGHT_STICK:
                return (state.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
            case XInputButtonEnum::LEFT_SHOULDER:
                return (state.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            case XInputButtonEnum::RIGHT_SHOULDER:
                return (state.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
            case XInputButtonEnum::BUTTON_A:
                return (state.wButtons & XINPUT_GAMEPAD_A) != 0;
            case XInputButtonEnum::BUTTON_B:
                return (state.wButtons & XINPUT_GAMEPAD_B) != 0;
            case XInputButtonEnum::BUTTON_X:
                return (state.wButtons & XINPUT_GAMEPAD_X) != 0;
            case XInputButtonEnum::BUTTON_Y:
                return (state.wButtons & XINPUT_GAMEPAD_Y) != 0;
            case XInputButtonEnum::LEFT_TRIGGER:
                return state.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
            case XInputButtonEnum::RIGHT_TRIGGER:
                return state.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
            case XInputButtonEnum::LEFT_STICK_UP:
                return state.sThumbLY > 0.6f;
            case XInputButtonEnum::LEFT_STICK_DOWN:
                return state.sThumbLY < 0.4f;
            case XInputButtonEnum::LEFT_STICK_LEFT:
                return state.sThumbLX < 0.4f;
            case XInputButtonEnum::LEFT_STICK_RIGHT:
                return state.sThumbLX > 0.6f;
            case XInputButtonEnum::RIGHT_STICK_UP:
                return state.sThumbRY > 0.6f;
            case XInputButtonEnum::RIGHT_STICK_DOWN:
                return state.sThumbRY < 0.4f;
            case XInputButtonEnum::RIGHT_STICK_LEFT:
                return state.sThumbRX < 0.4f;
            case XInputButtonEnum::RIGHT_STICK_RIGHT:
                return state.sThumbRX > 0.6f;
            default:
                break;
        }

        return false;
    }

    float XInputManager::get_analog_state(uint8_t player, XInputAnalogEnum analog) {
        XINPUT_GAMEPAD_STATE_NORMALIZED state;
    
        get_state(player, state);
        switch (analog) {
            case XInputAnalogEnum::LEFT_TRIGGER:
                return state.bLeftTrigger;
            case XInputAnalogEnum::RIGHT_TRIGGER:
                return state.bRightTrigger;
            case XInputAnalogEnum::LEFT_STICK_X:
                return state.sThumbLX;
            case XInputAnalogEnum::LEFT_STICK_Y:
                return state.sThumbLY;
            case XInputAnalogEnum::RIGHT_STICK_X:
                return state.sThumbRX;
            case XInputAnalogEnum::RIGHT_STICK_Y:
                return state.sThumbRY;
            default:
                break;
        }

        return 0.5f;
    }

    bool XInputManager::get_any_button_pressed(XINPUT_NEW_BUTTON &button) {
        for (uint8_t player = 0; player < XUSER_MAX_COUNT; player++) {
            for (uint16_t b = 0; b < static_cast<uint16_t>(XInputButtonEnum::COUNT); b++) {
                if (is_button_pressed(player, static_cast<XInputButtonEnum>(b))) {
                    button.player = player;
                    button.button = static_cast<XInputButtonEnum>(b);
                    return true;
                }
            }
        }
        return false;
    }

    void XInputManager::set_output_state(uint8_t player, XInputOutputEnum output, float value) {
        if (!this->initialized) {
            return;
        }
        if (player >= XUSER_MAX_COUNT) {
            return;
        }
        XINPUT_VIBRATION vibration = {};
        if (output == XInputOutputEnum::LEFT_RUMBLE) {
            vibration.wLeftMotorSpeed = static_cast<uint16_t>(value * 65535);
        } else if (output == XInputOutputEnum::RIGHT_RUMBLE) {
            vibration.wRightMotorSpeed = static_cast<uint16_t>(value * 65535);
        }
        XInputSetState_addr(player, &vibration);
    }

#endif

}
