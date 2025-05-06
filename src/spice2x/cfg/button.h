#pragma once

#include <string>
#include <utility>
#include <vector>

#include "api.h"

namespace rawinput {
    class RawInputManager;
}

enum ButtonAnalogType {
    BAT_NONE = 0,
    BAT_POSITIVE = 1,
    BAT_NEGATIVE = 2,
    BAT_HS_UP = 3,
    BAT_HS_UPRIGHT = 4,
    BAT_HS_RIGHT = 5,
    BAT_HS_DOWNRIGHT = 6,
    BAT_HS_DOWN = 7,
    BAT_HS_DOWNLEFT = 8,
    BAT_HS_LEFT = 9,
    BAT_HS_UPLEFT = 10,
    BAT_HS_NEUTRAL = 11,
    BAT_MIDI_CTRL_PRECISION = 12,
    BAT_MIDI_CTRL_SINGLE = 13,
    BAT_MIDI_CTRL_ONOFF = 14,
    BAT_MIDI_PITCH_DOWN = 15,
    BAT_MIDI_PITCH_UP = 16,
    BAT_ANY = 17,
};

extern const char *ButtonAnalogTypeStr[];

constexpr unsigned short INVALID_VKEY = UINT16_C(0xFF);

class Button {
private:
    std::vector<Button> alternatives;
    std::string name;
    std::string device_identifier = "";
    unsigned short vKey = INVALID_VKEY;
    ButtonAnalogType analog_type = BAT_NONE;
    double debounce_up = 0.0;
    double debounce_down = 0.0;
    bool invert = false;

    GameAPI::Buttons::State last_state = GameAPI::Buttons::BUTTON_NOT_PRESSED;
    float last_velocity = 0.f;
    unsigned short velocity_threshold = 0;

    std::string getVKeyString();
    std::string getMidiNoteString();

public:

    // overrides
    bool override_enabled = false;
    GameAPI::Buttons::State override_state = GameAPI::Buttons::BUTTON_NOT_PRESSED;
    float override_velocity = 0.f;

    explicit Button(std::string name) : name(std::move(name)) {};

    inline std::vector<Button> &getAlternatives() {
        return this->alternatives;
    }

    inline bool isSet() {
        if (this->override_enabled) {
            return true;
        }
        if (this->vKey != INVALID_VKEY) {
            return true;
        }

        for (auto &alternative : this->alternatives) {
            if (alternative.vKey != INVALID_VKEY) {
                return true;
            }
        }

        return false;
    }

    inline void clearBindings() {
        vKey = INVALID_VKEY;
        alternatives.clear();
        device_identifier = "";
        analog_type = BAT_NONE;
    }

    std::string getDisplayString(rawinput::RawInputManager* manager);

    inline bool isNaive() const {
        return this->device_identifier.empty();
    }

    inline const std::string &getName() const {
        return this->name;
    }

    inline const std::string &getDeviceIdentifier() const {
        return this->device_identifier;
    }

    inline void setDeviceIdentifier(std::string new_device_identifier) {
        this->device_identifier = std::move(new_device_identifier);
    }

    inline unsigned short getVKey() const {
        return this->vKey;
    }

    inline void setVKey(unsigned short vKey) {
        this->vKey = vKey;
    }

    inline ButtonAnalogType getAnalogType() const {
        return this->analog_type;
    }

    inline void setAnalogType(ButtonAnalogType analog_type) {
        this->analog_type = analog_type;
    }

    inline double getDebounceUp() const {
        return this->debounce_up;
    }

    inline void setDebounceUp(double debounce_time_up) {
        this->debounce_up = debounce_time_up;
    }

    inline double getDebounceDown() const {
        return this->debounce_down;
    }

    inline void setDebounceDown(double debounce_time_down) {
        this->debounce_down = debounce_time_down;
    }

    inline bool getInvert() const {
        return this->invert;
    }

    inline void setInvert(bool invert) {
        this->invert = invert;
    }

    inline GameAPI::Buttons::State getLastState() {
        return this->last_state;
    }

    inline void setLastState(GameAPI::Buttons::State last_state) {
        this->last_state = last_state;
    }

    inline float getLastVelocity() {
        return this->last_velocity;
    }

    inline void setLastVelocity(float last_velocity) {
        this->last_velocity = last_velocity;
    }

    inline unsigned short getVelocityThreshold() const {
        return this->velocity_threshold;
    }

    inline void setVelocityThreshold(unsigned short velocity_threshold) {
        this->velocity_threshold = velocity_threshold;
    }

    void getMidiVKey(int& channel, int& index);
    void setMidiVKey(rawinput::RawInputManager* manager, bool is_note, int channel, int index);

    /*
     * Map hat switch float value from [0-1] to directions.
     * Buffer must be sized 3 or bigger.
     * Order of detection is:
     * 1. Multi Direction Binding (example: BAT_HS_UPRIGHT)
     * 2. Lower number binding (in order up, right, down, left)
     * 3. Higher number binding
     * Empty fields will be 0 (BAT_NONE)
     *
     * Example:
     * Xbox360 Controller reports value 2 => mapped to float [0-1] it's 0.25f
     * Resulting buffer is: {BAT_HS_UPRIGHT, BAT_HS_UP, BAT_HS_RIGHT}
     */
    static void getHatSwitchValues(float analog_state, ButtonAnalogType* buffer);
};
