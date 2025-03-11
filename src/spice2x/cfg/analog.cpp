#include "analog.h"

#include <numeric>

#include <math.h>

#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/time.h"
#include "util/utils.h"

std::string Analog::getDisplayString(rawinput::RawInputManager *manager) {

    // device must be existing
    if (this->device_identifier.empty()) {
        return "";
    }

    // get index string
    auto index = this->getIndex();
    std::string indexString = fmt::format("{:#x}", index);

    // get device
    auto device = manager->devices_get(this->device_identifier);
    if (!device) {
        return "Device missing (" + indexString + ")";
    }

    // return string based on device type
    switch (device->type) {
        case rawinput::MOUSE: {
            const char *name;
            switch (index) {
                case rawinput::MOUSEPOS_X:
                    name = "X";
                    break;
                case rawinput::MOUSEPOS_Y:
                    name = "Y";
                    break;
                case rawinput::MOUSEPOS_WHEEL:
                    name = "Scroll Wheel";
                    break;
                default:
                    name = "?";
                    break;
            }
            return fmt::format("{} ({})", name, device->desc);
        }
        case rawinput::HID: {
            auto hid = device->hidInfo;
            if (index < hid->value_caps_names.size()) {
                return hid->value_caps_names[index] + " (" + device->desc + ")";
            }
            return "Invalid Axis (" + indexString + ")";
        }
        case rawinput::MIDI: {
            auto midi = device->midiInfo;
            // update strings in button.cpp as well
            if (index < midi->controls_precision.size()) {
                const int channel = (index / 32) + 1;
                const int cc_index = (index % 32);
                return fmt::format("MIDI Prec Ctrl Ch.{} CC#{} ({})", channel, cc_index, device->desc);
            } else if (index < midi->controls_precision.size() + midi->controls_single.size()) {
                const int index_rel = index - midi->controls_precision.size();
                const int channel = (index_rel / 44) + 1;
                int cc_index = (index_rel % 44);
                if (cc_index < 26) {
                    cc_index += 0x46; // single byte range
                } else {
                    cc_index = cc_index - 26 + 0x66; // undefined single byte range
                }
                return fmt::format("MIDI Ctrl Ch.{} CC#{} ({})", channel, cc_index, device->desc);
            } else if (index < midi->controls_precision.size() + midi->controls_single.size()
                                                               + midi->controls_onoff.size())
            {
                const int index_rel = index - midi->controls_precision.size() - midi->controls_single.size();
                const int channel = (index_rel / 6) + 1;
                const int cc_index = (index_rel % 6) + 0x40;
                return fmt::format("MIDI OnOff Ch.{} CC#{} ({})", channel, cc_index, device->desc);
            } else if (index <
                midi->controls_precision.size() + midi->controls_single.size() + midi->controls_onoff.size() + midi->pitch_bend.size())
            {
                const int index_rel =
                    index -
                    midi->controls_precision.size() -
                    midi->controls_single.size() -
                    midi->controls_onoff.size();
                return fmt::format("MIDI Pitch Ch.{} ({})", index_rel + 1, device->desc);
            } else {
                return "MIDI Unknown Index " + indexString + " (" + device->desc + ")";
            }
        }
        case rawinput::DESTROYED:
            return "Device unplugged (" + indexString + ")";
        default:
            return "Unknown Axis (" + indexString + ")";
    }
}

float Analog::getSmoothedValue(float raw_rads) {
    auto now = get_performance_milliseconds();

    // prevent extremely frequent polling
    if ((now - vector_history.at(vector_history_index).time_in_ms) < 0.9) {
        return smoothed_last_state;
    }

    // calculate derived values for the newly-read analog value
    vector_history_index = (vector_history_index + 1) % vector_history.size();
    auto &current  = vector_history.at(vector_history_index);
    current.time_in_ms = now;
    current.sine = sin(raw_rads);
    current.cosine = cos(raw_rads);

    // calculated the weighted sum of sines and cosines
    auto sines = 0.f;
    auto cosines = 0.f;
    for (auto &vector : vector_history) {
        auto time_diff = now - vector.time_in_ms;
        // time from QPC should never roll backwards, but just in case
        if (time_diff < 0.f) {
            time_diff = 0.f;
        }

        // the weight falls of linearly; value from 24ms ago counts as half, 48ms ago counts as 0
        double weight = (-time_diff / 48.f) + 1.f;
        if (weight > 0.f) {
            sines += weight * vector.sine;
            cosines += weight * vector.cosine;
        }
    }

    // add a tiny bit so that cosine is never 0.0f when fed to atan2
    if (cosines == 0.f) {
        cosines = std::nextafter(0.f, 1.f);
    }

    // average for angles:
    // arctan[(sum of sines of all angles) / (sum of cosines of all angles)]
    // atan2 will give [-pi, +pi], so normalize to make [0, 2pi]
    smoothed_last_state = normalizeAngle(atan2(sines, cosines));
    return smoothed_last_state;
}

float Analog::calculateAngularDifference(float old_rads, float new_rads) {
    float delta = new_rads - old_rads;

    // assumes value doesn't change more than PI (180 deg) compared to last poll
    if (std::abs(delta) < M_PI) {
        return delta;
    } else {
        // use the coterminal angle instead
        if (delta < 0.f) {
            return M_TAU + delta;
        } else {
            return -(M_TAU - delta);
        }
    }
}

float Analog::applyAngularSensitivity(float raw_rads) {
    float delta = calculateAngularDifference(previous_raw_rads, raw_rads);
    previous_raw_rads = raw_rads;
    adjusted_rads = normalizeAngle(adjusted_rads + (delta * sensitivity));
    return adjusted_rads;
}

float Analog::normalizeAngle(float rads) {
    // normalizes radian value into [0, 2pi] range.
    // for small angles, this is MUCH faster than fmodf.
    float angle = rads;
    while (angle > M_TAU) {
        angle -= M_TAU;
    }
    while (angle < 0.f) {
        angle += M_TAU;
    }
    return angle;
}

float Analog::applyMultiplier(float value) {
    if (1 < this->multiplier) {
        // multiplier - just multiply the value and take the decimal part
        return normalizeAnalogValue(value * this->multiplier);
    } else if (this->multiplier < -1) {
        const unsigned short number_of_divisions = -this->multiplier;
        // divisor - need to take care of over/underflow
        if (0.75f < this->divisor_previous_value && value < 0.25f) {
            this->divisor_region = (this->divisor_region + 1) % number_of_divisions;
        } else if (this->divisor_previous_value < 0.25f && 0.75f < value) {
            if (1 <= this->divisor_region) {
                this->divisor_region -= 1;
            } else {
                this->divisor_region = number_of_divisions - 1;
            }
        }
        this->divisor_previous_value = value;
        return ((float)this->divisor_region + value) / (float)number_of_divisions;
    } else {
        // multiplier in [-1, 1] range is just treated as 1
        return value;
    }
}

float Analog::normalizeAnalogValue(float value) {
    // effectively the same as fmodf(value, 1.f)
    // for small values, this is MUCH faster than fmodf.
    float new_value = value;
    while (new_value > 1.f) {
        new_value -= 1.f;
    }
    while (new_value < 0.f) {
        new_value += 1.f;
    }
    return new_value;
}

float Analog::applyDeadzone(float raw_value) {
    float value = raw_value;
    const auto deadzone = this->getDeadzone();
    if (deadzone > 0) {

        // calculate values
        const auto delta = value - 0.5f;
        const auto dtlen = 1.f - deadzone;

        // check mirror
        if (this->getDeadzoneMirror()) {

            // deadzone on the edges
            if (dtlen != 0.f) {
                value = std::max(0.f, std::min(1.f, 0.5f + (delta / dtlen)));
            } else {
                value = 0.5f;
            }

        } else {

            // deadzone around the middle
            const auto limit = deadzone * 0.5f;
            if (dtlen != 0.f) {
                if (delta > limit) {
                    value = std::min(1.f, 0.5f + std::max(0.f, (delta - limit) / dtlen));
                } else if (delta < -limit) {
                    value = std::max(0.f, 0.5f + std::min(0.f, (delta + limit) / dtlen));
                } else {
                    value = 0.5f;
                }
            } else {
                value = 0.5f;
            }
        }

    } else if (deadzone < 0) {

        // invert for mirror
        if (this->getDeadzoneMirror()) {
            value = 1.f - value;
        }

        // deadzone from minimum value
        if (deadzone > -1 && value > -deadzone) {
            value = std::min(1.f, (value + deadzone) / (1.f + deadzone));
        } else {
            value = 0.f;
        }

        // revert value for mirror
        if (this->getDeadzoneMirror()) {
            value = 1.f - value;
        }
    }
    return value;
}