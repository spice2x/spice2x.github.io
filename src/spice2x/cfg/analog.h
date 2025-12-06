#pragma once

#include <array>
#include <string>
#include <cmath>
#include <queue>

#define ANALOG_HISTORY_CNT 10
#define M_TAU (2 * M_PI)
#define M_1_TAU (0.5 * M_1_PI)

namespace rawinput {
    class RawInputManager;
}

struct AnalogMovingAverage {
    double time_in_ms;
    float sine;
    float cosine;
};

class Analog {
private:
    std::string name;
    std::string device_identifier = "";
    unsigned short index = USHRT_MAX;
    float sensitivity = 1.f;
    float deadzone = 0.f;
    bool deadzone_mirror = false;
    bool invert = false;
    float last_state = 0.5f;
    bool sensitivity_set = false;
    bool deadzone_set = false;
    
    // smoothing function
    bool smoothing = false;
    std::array<AnalogMovingAverage, ANALOG_HISTORY_CNT> vector_history;
    int vector_history_index = 0;
    float smoothed_last_state = 0.f;

    // angular scaling for sensitivity
    float previous_raw_rads = 0.f;
    float adjusted_rads = 0.f;

    // multiplier/divisor
    int multiplier = 1; 
    float divisor_previous_value = 0.5f;
    unsigned short divisor_region = 0;

    // relative input mode
    float absolute_value_for_rel_mode = 0.5f;
    bool relative_mode = false;

    // circular buffer (delayed input)
    int delay_buffer_depth = 0;
    std::queue<float> delay_buffer;

    float calculateAngularDifference(float old_rads, float new_rads);
    float normalizeAngle(float rads);
    float normalizeAnalogValue(float value);

public:

    // overrides
    bool override_enabled = false;
    float override_state = 0.5f;

    explicit Analog(std::string name) : name(std::move(name)) {
        vector_history.fill({0.0, 0.f, 0.f});
    };

    std::string getDisplayString(rawinput::RawInputManager* manager);
    float getSmoothedValue(float raw_rads);
    float applyAngularSensitivity(float raw_rads);
    float applyMultiplier(float raw_value);
    float applyDeadzone(float raw_value);

    inline bool isSet() {
        if (this->override_enabled) {
            return true;
        }
        return this->index != 0xFF;
    }

    inline void resetValues() {
        setSensitivity(1.f);
        setDeadzone(0.f);
        invert = false;
        smoothing = false;
        deadzone_mirror = false;
        setMultiplier(1);
        setRelativeMode(false);
        setDelayBufferDepth(0);
    }

    inline void clearBindings() {
        device_identifier = "";
        index = 0xFF;
        resetValues();
    }

    inline const std::string &getName() const {
        return this->name;
    }

    inline const std::string &getDeviceIdentifier() const {
        return this->device_identifier;
    }

    inline void setDeviceIdentifier(std::string device_identifier) {
        this->device_identifier = std::move(device_identifier);
    }

    inline unsigned short getIndex() const {
        return this->index;
    }

    inline void setIndex(unsigned short index) {
        this->index = index;
    }

    inline float getSensitivity() const {
        return this->sensitivity;
    }

    inline float getDeadzone() const {
        return this->deadzone;
    }

    inline void setSensitivity(float sensitivity) {
        this->sensitivity = sensitivity;
        this->sensitivity_set = (sensitivity < 0.99f || 1.01f < sensitivity);
    }

    inline void setDeadzone(float deadzone) {
        this->deadzone = std::max(-1.f, std::min(1.f, deadzone));
        this->deadzone_set = fabsf(deadzone) > 0.01f;
    }

    inline bool isSensitivitySet() {
        return this->sensitivity_set;
    }

    inline bool isDeadzoneSet() {
        return this->deadzone_set;
    }

    inline bool getDeadzoneMirror() const {
        return this->deadzone_mirror;
    }

    inline void setDeadzoneMirror(bool deadzone_mirror) {
        this->deadzone_mirror = deadzone_mirror;
    }

    inline bool getInvert() const {
        return this->invert;
    }

    inline void setInvert(bool invert) {
        this->invert = invert;
    }

    inline bool getSmoothing() const {
        return this->smoothing;
    }

    inline void setSmoothing(bool smoothing) {
        this->smoothing = smoothing;
    }

    inline int getMultiplier() const {
        return this->multiplier;
    }

    inline void setMultiplier(int multiplier) {
        this->multiplier = multiplier;
        this->divisor_region = 0;
        this->divisor_previous_value = 0.5f;
    }

    inline float getLastState() const {
        return this->last_state;
    }

    inline void setLastState(float last_state) {
        this->last_state = last_state;
    }

    inline bool isRelativeMode() const {
        return this->relative_mode;
    }

    inline void setRelativeMode(bool relative_mode) {
        this->relative_mode = relative_mode;
        this->absolute_value_for_rel_mode = 0.5f;
    }

    inline float getAbsoluteValue(float relative_delta) {
        this->absolute_value_for_rel_mode =
            normalizeAnalogValue(this->absolute_value_for_rel_mode + relative_delta);
        return this->absolute_value_for_rel_mode;
    }

    inline int getDelayBufferDepth() const {
        return this->delay_buffer_depth;
    }

    inline void setDelayBufferDepth(int depth) {
        this->delay_buffer_depth = depth;
    }

    inline std::queue<float> &getDelayBuffer() {
        return this->delay_buffer;
    }
};
