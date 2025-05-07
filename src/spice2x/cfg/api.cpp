#include "api.h"

#include <optional>

#include "launcher/superexit.h"
#include "rawinput/rawinput.h"
#include "rawinput/piuio.h"
#include "util/time.h"
#include "util/utils.h"

#include "config.h"

using namespace GameAPI;

std::vector<Button> GameAPI::Buttons::getButtons(const std::string &game_name) {
    return Config::getInstance().getButtons(game_name);
}

std::vector<Button> GameAPI::Buttons::getButtons(Game *game) {
    return Config::getInstance().getButtons(game);
}

static Buttons::State getMidiV2ButtonState(float last_on_time, float last_off_time);

std::vector<Button> GameAPI::Buttons::sortButtons(
        const std::vector<Button> &buttons,
        const std::vector<std::string> &button_names,
        const std::vector<unsigned short> *vkey_defaults)
{
    std::vector<Button> sorted;

    bool button_found;
    int index = 0;
    for (auto &name : button_names) {
        button_found = false;

        for (auto &bt : buttons) {
            if (name == bt.getName()) {
                button_found = true;
                sorted.push_back(bt);
                break;
            }
        }

        if (!button_found) {
            auto &button = sorted.emplace_back(name);

            if (vkey_defaults) {
                button.setVKey(vkey_defaults->at(index));
            }
        }

        ++index;
    }

    return sorted;
}

GameAPI::Buttons::State GameAPI::Buttons::getState(rawinput::RawInputManager *manager, Button &_button, bool check_alts) {

    // check override
    if (_button.override_enabled) {
        return _button.override_state;
    }

    // for iterating button alternatives
    auto current_button = &_button;
    auto alternatives = check_alts ? &current_button->getAlternatives() : nullptr;
    unsigned int button_count = 0;
    std::optional<bool> window_has_focus;
    while (true) {

        // naive behavior
        if (current_button->isNaive()) {
            GameAPI::Buttons::State state;
            auto vkey = current_button->getVKey();

            // check for focus
            if (vkey != INVALID_VKEY && rawinput::NAIVE_REQUIRE_FOCUS) {
                if (!window_has_focus.has_value()) {
                    window_has_focus = superexit::has_focus();
                }
                if (!window_has_focus.value()) {
                    vkey = INVALID_VKEY;
                }
            }

            // read
            if (vkey == INVALID_VKEY) {
                state = BUTTON_NOT_PRESSED;
            } else {
                state = (GetAsyncKeyState(vkey) & 0x8000) ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
            }

            // invert
            if (current_button->getInvert()) {
                if (state == BUTTON_PRESSED) {
                    state = BUTTON_NOT_PRESSED;
                } else {
                    state = BUTTON_PRESSED;
                }
            }

            // return state
            if (state != BUTTON_NOT_PRESSED) {
                return state;
            }

            // get next button
            button_count++;
            if (!alternatives || alternatives->empty() || button_count - 1 >= alternatives->size()) {
                return BUTTON_NOT_PRESSED;
            } else {
                current_button = &alternatives->at(button_count - 1);
                continue;
            }
        }

        // get device
        auto &devid = current_button->getDeviceIdentifier();
        auto device = manager->devices_get(devid, false); // TODO: fix to update only

        // check for focus
        if (device && rawinput::RAWINPUT_REQUIRE_FOCUS) {
            if (!window_has_focus.has_value()) {
                window_has_focus = superexit::has_focus();
            }
            if (!window_has_focus.value()) {
                device = nullptr;
            }
        }

        // get state if device was marked as updated
        GameAPI::Buttons::State state = current_button->getLastState();
        double *last_up = nullptr;
        double *last_down = nullptr;
        if (device) {

            // lock device
            device->mutex->lock();

            // get vkey
            auto vKey = current_button->getVKey();

            // update state based on device type
            switch (device->type) {
                case rawinput::MOUSE: {
                    if (vKey < sizeof(device->mouseInfo->key_states)) {
                        auto mouse = device->mouseInfo;
                        state = mouse->key_states[vKey] ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                        last_up = &mouse->key_up[vKey];
                        last_down = &mouse->key_down[vKey];
                    }
                    break;
                }
                case rawinput::KEYBOARD: {
                    if (vKey < sizeof(device->keyboardInfo->key_states)) {
                        auto kb = device->keyboardInfo;
                        state = kb->key_states[vKey] ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                        last_up = &kb->key_up[vKey];
                        last_down = &kb->key_down[vKey];
                    }
                    break;
                }
                case rawinput::HID: {
                    auto hid = device->hidInfo;
                    auto bat = current_button->getAnalogType();
                    switch (bat) {
                        case BAT_NONE: {
                            auto button_states_it = hid->button_states.begin();
                            auto button_up_it = hid->button_up.begin();
                            auto button_down_it = hid->button_down.begin();
                            while (button_states_it != hid->button_states.end()) {
                                auto size = button_states_it->size();
                                if (vKey < size) {
                                    state = (*button_states_it)[vKey] ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                    last_up = &(*button_up_it)[vKey];
                                    last_down = &(*button_down_it)[vKey];
                                    break;
                                } else {
                                    vKey -= size;
                                    ++button_states_it;
                                    ++button_up_it;
                                    ++button_down_it;
                                }
                            }
                            break;
                        }
                        case BAT_NEGATIVE:
                        case BAT_POSITIVE:
                        case BAT_ANY: {
                            auto value_states = &hid->value_states;
                            if (vKey < value_states->size()) {
                                auto value = value_states->at(vKey);
                                if (current_button->getAnalogType() == BAT_POSITIVE) {
                                    state = value > 0.6f ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                } else if (current_button->getAnalogType() == BAT_NEGATIVE) {
                                    state = value < 0.4f ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                } else {
                                    state = value > 0.01f ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                }
                            } else {
                                state = BUTTON_NOT_PRESSED;
                            }
                            break;
                        }
                        case BAT_HS_UP:
                        case BAT_HS_UPRIGHT:
                        case BAT_HS_RIGHT:
                        case BAT_HS_DOWNRIGHT:
                        case BAT_HS_DOWN:
                        case BAT_HS_DOWNLEFT:
                        case BAT_HS_LEFT:
                        case BAT_HS_UPLEFT:
                        case BAT_HS_NEUTRAL: {
                            auto &value_states = hid->value_states;
                            if (vKey < value_states.size()) {
                                auto value = value_states.at(vKey);

                                // get hat switch values
                                ButtonAnalogType buffer[3];
                                Button::getHatSwitchValues(value, buffer);

                                // check if one of the values match our analog type
                                state = BUTTON_NOT_PRESSED;
                                for (ButtonAnalogType &buffer_bat : buffer) {
                                    if (buffer_bat == bat) {
                                        state = BUTTON_PRESSED;
                                        break;
                                    }
                                }

                            } else
                                state = BUTTON_NOT_PRESSED;
                            break;
                        }
                        default:
                            state = BUTTON_NOT_PRESSED;
                            break;
                    }
                    break;
                }
                case rawinput::MIDI: {
                    auto bat = current_button->getAnalogType();
                    auto midi = device->midiInfo;
                    switch (bat) {
                        case BAT_NONE: {
                            if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                                // spicetools legacy midi logic: use event log
                                //
                                // drums send NOTE_ON and NOTE_OFF in rapid succession, before game engine has a chance
                                // to poll for it - to address this, keep a counter (states_events array) and the last
                                // state (states array), incrementing the states_events on rising edges (NOTE_ON)
                                // and popping events off the queue every time it's checked.
                                //
                                // if the same drum pad is mapped to multiple buttons, multiple issues arise:
                                //  1. we run through this logic for each button, which consumes an event every time;
                                //     therefore, the first button may see the ON event, but subsequent mappings may
                                //     completely miss it as it already has been drained
                                //  2. it is impossible to implement velocity threshold with this logic since the
                                //     velocity is a per-note value that goes away as soon as NOTE_OFF is detected
                                if (vKey < midi->states_events.size()) {
                                    // check for event
                                    auto midi_event = midi->states_events[vKey];
                                    if (midi_event) {

                                        // choose state based on event
                                        state = (midi_event % 2) ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;

                                        // update event
                                        if (!midi->states[vKey] || midi_event > 1) {
                                            midi->states_events[vKey]--;
                                        }
                                    } else {
                                        state = BUTTON_NOT_PRESSED;
                                    }
                                }
                            } else {
                                // spice2x midi logic (new!)
                                //
                                // for every MIDI NOTE ON message, latch the "on" for a certain time, even if NOTE
                                // OFF message is seen immediately afterwards.
                                //
                                // each ON event is held long enough for the game's input poll to see it (e.g., gitadora
                                // polls every 16ms or so, rawinput holds it for 20ms by default)
                                //
                                // this is much simpler and does not have the issues mentioned above for the legacy
                                // logic, however the downside is that there is a risk of coalescing rapid inputs into
                                // one.
                                //
                                // that being said:
                                //  * default value of 20ms should be reasonable; humans can't realistically hit the
                                //    same note faster than this; in fact it's likely to be a misfire
                                //  * we can tweak it per-game if needed to suit the game's polling period (in the
                                //    future)
                                //  * as a last resort the user can always override it via the option (MidiNoteSustain)
                                if (vKey < midi->v2_last_on_time.size()) {

                                    // take the velocity threshold from first button binding we encounter here
                                    // this hardware key may be mapped to multiple bindings, but the UI should keep them
                                    // the same value, as only one threshold value can be set per MIDI key
                                    // (otherwise it makes the sustain logic too complicated)
                                    const auto sw_threshold = current_button->getVelocityThreshold();
                                    if (0 < sw_threshold && !midi->v2_velocity_threshold_set_on_device[vKey]) {
                                        midi->v2_velocity_threshold_set_on_device[vKey] = true;
                                        midi->v2_velocity_threshold[vKey] = sw_threshold;
                                    }

                                    state = getMidiV2ButtonState(
                                                midi->v2_last_on_time[vKey],
                                                midi->v2_last_off_time[vKey]);

                                } else {
                                    state = BUTTON_NOT_PRESSED;
                                }
                            }
                            break;
                        }
                        case BAT_MIDI_CTRL_PRECISION: {
                            if (vKey < midi->controls_precision.size()) {
                                if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                                    state = midi->controls_precision[vKey] > 0 ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                } else {
                                    // not using getVelocityHelper here to avoid locking and other checks
                                    const auto v = device->midiInfo->controls_precision[vKey];
                                    // velocity threshold ranges from [0, 127], so do some math for double precision
                                    const auto threshold = (current_button->getVelocityThreshold() << 7u) | 0x7f;
                                    state = (threshold < v) ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                }
                            } else {
                                state = BUTTON_NOT_PRESSED;
                            }
                            break;
                        }
                        case BAT_MIDI_CTRL_SINGLE: {
                            if (vKey < midi->controls_single.size()) {
                                if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                                    state = midi->controls_single[vKey] > 0 ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                } else {
                                    // not using getVelocityHelper here to avoid locking and other checks
                                    const auto v = device->midiInfo->controls_single[vKey];
                                    state = (current_button->getVelocityThreshold() < v) ?
                                        BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                }
                            } else {
                                state = BUTTON_NOT_PRESSED;
                            }
                            break;
                        }
                        case BAT_MIDI_CTRL_ONOFF: {
                            if (vKey < midi->controls_onoff.size()) {
                                if (rawinput::get_midi_algorithm() == rawinput::MidiNoteAlgorithm::LEGACY) {
                                    state = midi->controls_onoff[vKey] ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                                } else {
                                    state = getMidiV2ButtonState(
                                                midi->v2_controls_onoff_last_on_time[vKey],
                                                midi->v2_controls_onoff_last_off_time[vKey]);
                                }
                            } else {
                                state = BUTTON_NOT_PRESSED;
                            }
                            break;
                        }
                        case BAT_MIDI_PITCH_DOWN:
                            if (vKey < midi->pitch_bend.size()) {
                                state = midi->pitch_bend[vKey] < 0 ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                            } else {
                                state = BUTTON_NOT_PRESSED;
                            }
                            break;
                        case BAT_MIDI_PITCH_UP:
                            if (vKey < midi->pitch_bend.size()) {
                                state = midi->pitch_bend[vKey] > 0 ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                            } else {
                                state = BUTTON_NOT_PRESSED;
                            }
                            break;
                        default: {
                            state = BUTTON_NOT_PRESSED;
                            break;
                        }
                    }
                    break;
                }
                case rawinput::PIUIO_DEVICE: {
                    state = device->piuioDev->IsPressed(vKey) ? BUTTON_PRESSED : BUTTON_NOT_PRESSED;
                }
                default:
                    break;
            }

            // unlock device
            device->mutex->unlock();
        }

        // debounce
        if (state == BUTTON_NOT_PRESSED) {
            if (last_up) {
                auto debounce_up = current_button->getDebounceUp();
                if (debounce_up > 0.0 && get_performance_seconds() - *last_up < debounce_up) {
                    state = BUTTON_PRESSED;
                }
            }
        } else {
            if (last_down) {
                auto debounce_down = current_button->getDebounceDown();
                if (debounce_down > 0.0 && get_performance_seconds() - *last_down < debounce_down) {
                    state = BUTTON_NOT_PRESSED;
                }
            }
        }

        // set last state
        current_button->setLastState(state);

        // invert
        if (current_button->getInvert()) {
            if (state == BUTTON_PRESSED) {
                state = BUTTON_NOT_PRESSED;
            } else {
                state = BUTTON_PRESSED;
            }
        }

        // early quit
        if (state == BUTTON_PRESSED) {
            return state;
        }

        // get next button
        button_count++;
        if (!alternatives || alternatives->empty() || button_count - 1 >= alternatives->size()) {
            return BUTTON_NOT_PRESSED;
        } else {
            current_button = &alternatives->at(button_count - 1);
        }
    }
}

Buttons::State Buttons::getState(std::unique_ptr<rawinput::RawInputManager> &manager, Button &button, bool check_alts) {
    if (manager) {
        return getState(manager.get(), button, check_alts);
    } else {
        return button.getLastState();
    }
}

static float getVelocityHelper(rawinput::RawInputManager *manager, Button &button) {

    // check override
    if (button.override_enabled) {
        return button.override_velocity;
    }

    // naive behavior
    if (button.isNaive()) {
        if (button.getInvert()) {
            return (GetAsyncKeyState(button.getVKey()) & 0x8000) ? 0.f : 1.f;
        } else {
            return (GetAsyncKeyState(button.getVKey()) & 0x8000) ? 1.f : 0.f;
        }
    }

    // get button state
    Buttons::State button_state = Buttons::getState(manager, button, false);

    // check if button isn't being pressed
    if (button_state != Buttons::BUTTON_PRESSED) {
        return 0.f;
    }

    // get device
    auto &devid = button.getDeviceIdentifier();
    auto device = manager->devices_get(devid, false);

    // return last velocity if device wasn't found
    if (!device) {
        return button.getLastVelocity();
    }

    // prepare
    float velocity = 1.f;
    auto vKey = button.getVKey();

    // lock device
    device->mutex->lock();

    // determine velocity based on device type
    if (device->type == rawinput::MIDI) {
        switch (button.getAnalogType()) {
            case ButtonAnalogType::BAT_MIDI_CTRL_PRECISION:
                if (vKey < device->midiInfo->controls_precision.size()) {
                    velocity = device->midiInfo->controls_precision[vKey] / 16383.f;
                } else {
                    velocity = 0.f;
                }
                break;

            case ButtonAnalogType::BAT_MIDI_CTRL_SINGLE:
                if (vKey < device->midiInfo->controls_single.size()) {
                    velocity = device->midiInfo->controls_single[vKey] / 127.f;
                } else {
                    velocity = 0.f;
                }
                break;

            case ButtonAnalogType::BAT_MIDI_CTRL_ONOFF:
                if (vKey < device->midiInfo->controls_onoff.size()) {
                    velocity = device->midiInfo->controls_onoff[vKey] ? 1.f : 0.f;
                } else {
                    velocity = 0.f;
                }
                break;

            case ButtonAnalogType::BAT_MIDI_PITCH_DOWN:
                if (vKey < device->midiInfo->pitch_bend.size()) {
                    velocity = device->midiInfo->pitch_bend[vKey] < 0 ? 1.f : 0.f;
                } else {
                    velocity = 0.f;
                }
                break;

            case ButtonAnalogType::BAT_MIDI_PITCH_UP:
                if (vKey < device->midiInfo->pitch_bend.size()) {
                    // pitch range is [-8192, 8191]
                    velocity = (device->midiInfo->pitch_bend[vKey]) > 0 ? 1.f : 0.f;
                } else {
                    velocity = 0.f;
                }
                break;

            case ButtonAnalogType::BAT_NONE:
            default:
                // velocity sensitive
                if (vKey < device->midiInfo->velocity.size()) {
                    velocity = (float) device->midiInfo->velocity[vKey] / 127.f;
                } else {
                    velocity = 0.f;
                }
                break;
        }

        // invert
        if (button.getInvert()) {
            velocity = 1.f - velocity;
        }
    }

    // unlock device
    device->mutex->unlock();

    // set last velocity
    button.setLastVelocity(velocity);

    // return determined velocity
    return velocity;
}

float GameAPI::Buttons::getVelocity(rawinput::RawInputManager *manager, Button &button) {

    // get button velocity
    auto velocity = getVelocityHelper(manager, button);

    // check alternatives
    for (auto &alternative : button.getAlternatives()) {
        auto alt_velocity = getVelocityHelper(manager, alternative);
        if (alt_velocity > velocity) {
            velocity = alt_velocity;
        }
    }

    // return highest velocity detected
    return velocity;
}

float Buttons::getVelocity(std::unique_ptr<rawinput::RawInputManager> &manager, Button &button) {
    if (manager) {
        return getVelocity(manager.get(), button);
    } else {
        return button.getLastVelocity();
    }
}

float GameAPI::Analogs::getState(rawinput::Device *device, Analog &analog) {
    float value = 0.5f;
    if (!device) {
        return value;
    }

    auto index = analog.getIndex();
    auto inverted = analog.getInvert();
    device->mutex->lock();

    // get value from device
    switch (device->type) {
        case rawinput::MOUSE: {

            // check for focus
            if (rawinput::NAIVE_REQUIRE_FOCUS && !superexit::has_focus()) {
                value = analog.getLastState();
                break;
            }

            // get mouse position
            auto mouse = device->mouseInfo;
            long pos;
            switch (index) {
                case rawinput::MOUSEPOS_X:
                    pos = mouse->pos_x;
                    break;
                case rawinput::MOUSEPOS_Y:
                    pos = mouse->pos_y;
                    break;
                case rawinput::MOUSEPOS_WHEEL:
                    pos = mouse->pos_wheel;
                    break;
                default:
                    pos = 0;
                    break;
            }

            // apply sensitivity
            auto val = (int) roundf(pos * analog.getSensitivity());
            if (val < 0) {
                inverted = !inverted;
            }

            // modulo & normalize to [0.0, 1.0]
            if (index != rawinput::MOUSEPOS_WHEEL) {
                val = std::abs(val) % 257;
                value = val / 256.f;
            } else {
                val = std::abs(val) % 65;
                value = val / 64.f;
            }

            // invert
            if (inverted) {
                value = 1.f - value;
            }

            break;
        }
        case rawinput::HID: {

            // get value
            if (inverted) {
                value = 1.f - device->hidInfo->value_states[index];
            } else {
                value = device->hidInfo->value_states[index];
            }

            // deadzone
            if (analog.isDeadzoneSet()) {
                value = analog.applyDeadzone(value);
            }

            if (analog.isRelativeMode()) {
                float relative_delta = value - 0.5f;
                // built-in scaling to make values reasonable
                relative_delta /= 80.f;

                // integer multiplier/divisor
                const auto mult = analog.getMultiplier();
                if (mult < -1) {
                    relative_delta /= -mult;
                } else if (1 < mult) {
                    relative_delta *= mult;
                }

                // sensitivity (ranges from 0.0 to 4.0)
                if (analog.isSensitivitySet()) {
                    relative_delta *= analog.getSensitivity();
                }

                // translate relative movement to absolute value
                value = analog.getAbsoluteValue(relative_delta);

            } else {
                // integer multiplier
                value = analog.applyMultiplier(value);

                // smoothing/sensitivity
                if (analog.getSmoothing() || analog.isSensitivitySet()) {
                    float rads = value * (float) M_TAU;

                    // smoothing
                    if (analog.getSmoothing()) {

                        // preserve direction
                        if (rads >= M_TAU) {
                            rads -= 0.0001f;
                        }

                        // calculate angle
                        rads = analog.getSmoothedValue(rads);
                    }

                    // sensitivity
                    if (analog.isSensitivitySet()) {
                        rads = analog.applyAngularSensitivity(rads);
                    }

                    // apply to value
                    value = rads * (float) M_1_TAU;
                }
            }

            // delay
            if (0 < analog.getDelayBufferDepth()) {
                auto& queue = analog.getDelayBuffer();

                // ensure the queue isn't too long; drop old values
                while (analog.getDelayBufferDepth() <= (int)queue.size()) {
                    queue.pop();
                }

                // always push new value
                queue.push(value);

                // get a new value to return
                if ((int)queue.size() < analog.getDelayBufferDepth()) {
                    // not enough in the queue, stall for now, shouldn't happen often
                    value = analog.getLastState();
                } else {
                    value = queue.front();
                    queue.pop();
                }
            }

            break;
        }
        case rawinput::MIDI: {

            // get sizes
            auto midi = device->midiInfo;
            auto prec_count = (int) midi->controls_precision.size();
            auto single_count = (int) midi->controls_single.size();
            auto onoff_count = (int) midi->controls_onoff.size();
            auto pitch_count = (int) midi->pitch_bend.size();

            // decide on value
            if (index < prec_count)
                value = midi->controls_precision[index] / 16383.f;
            else if (index < prec_count + single_count)
                value = midi->controls_single[index - prec_count] / 127.f;
            else if (index < prec_count + single_count + onoff_count)
                value = midi->controls_onoff[index - prec_count - single_count] ? 1.f : 0.f;
            else if (index < prec_count + single_count + onoff_count + pitch_count)
                value = (midi->pitch_bend[index - prec_count - single_count - onoff_count] + 0x2000) / 16383.f;

            // invert value
            if (inverted) {
                value = 1.f - value;
            }

            // deadzone
            if (analog.isDeadzoneSet()) {
                value = analog.applyDeadzone(value);
            }
            break;
        }
        default:
            break;
    }

    device->mutex->unlock();

    return value;
}

std::vector<Analog> GameAPI::Analogs::getAnalogs(const std::string &game_name) {
    return Config::getInstance().getAnalogs(game_name);
}

std::vector<Analog> GameAPI::Analogs::sortAnalogs(
        const std::vector<Analog> &analogs,
        const std::vector<std::string> &analog_names)
{
    std::vector<Analog> sorted;

    bool analog_found;
    for (auto &name : analog_names) {
        analog_found = false;

        for (auto &analog : analogs) {
            if (name == analog.getName()) {
                analog_found = true;
                sorted.push_back(analog);
                break;
            }
        }

        if (!analog_found) {
            sorted.emplace_back(name);
        }
    }

    return sorted;
}

float GameAPI::Analogs::getState(rawinput::RawInputManager *manager, Analog &analog) {

    // check override
    if (analog.override_enabled) {
        return analog.override_state;
    }

    // get device
    auto &devid = analog.getDeviceIdentifier();
    auto device = manager->devices_get(devid, false); // TODO: fix to update only

    // return last state if device wasn't updated
    if (!device) {
        return analog.getLastState();
    }

    float state = getState(device, analog);
    analog.setLastState(state);

    return state;
}

float Analogs::getState(std::unique_ptr<rawinput::RawInputManager> &manager, Analog &analog) {
    if (manager) {
        return getState(manager.get(), analog);
    } else {
        return analog.getLastState();
    }
}

std::vector<Light> GameAPI::Lights::getLights(const std::string &game_name) {
    return Config::getInstance().getLights(game_name);
}

std::vector<Light> GameAPI::Lights::sortLights(
        const std::vector<Light> &lights,
        const std::vector<std::string> &light_names)
{
    std::vector<Light> sorted;

    bool light_found;
    for (auto &name : light_names) {
        light_found = false;

        for (auto &light : lights) {
            if (name == light.getName()) {
                light_found = true;
                sorted.push_back(light);
                break;
            }
        }

        if (!light_found) {
            sorted.emplace_back(name);
        }
    }

    return sorted;
}

void GameAPI::Lights::writeLight(rawinput::Device *device, int index, float value) {

    // check device
    if (!device) {
        return;
    }

    // clamp to range [0,1]
    value = CLAMP(value, 0.f, 1.f);

    // lock device
    device->mutex->lock();

    // enable output
    device->output_enabled = true;

    // check type
    switch (device->type) {
        case rawinput::HID: {
            auto hid = device->hidInfo;

            // find in buttons
            bool button_found = false;
            for (auto &button_states : hid->button_output_states) {
                if ((size_t) index < button_states.size()) {
                    auto new_state = value > 0.5f;
                    if (button_states[index] != new_state) {
                        button_states[index] = new_state;
                        device->output_pending = true;
                    }
                    button_found = true;
                    break;
                } else
                    index -= button_states.size();
            }

            // find in values
            if (!button_found) {
                auto &value_states = hid->value_output_states;
                if ((size_t) index < value_states.size()) {
                    auto cur_state = &value_states[index];
                    if (*cur_state != value) {
                        *cur_state = value;
                        device->output_pending = true;
                    }
                }
            }

            break;
        }
        case rawinput::SEXTET_OUTPUT: {
            if (index < rawinput::SextetDevice::LIGHT_COUNT) {
                device->sextetInfo->light_state[index] = value > 0;
                device->sextetInfo->push_light_state();
                device->output_pending = true;
            } else {
                log_warning("api", "invalid sextet light index: {}", index);
            }
            break;
        }
        case rawinput::PIUIO_DEVICE: {
            if (index < rawinput::PIUIO::PIUIO_MAX_NUM_OF_LIGHTS) {
                device->piuioDev->SetLight(index, value > 0);
                device->output_pending = true;
            } else {
                log_warning("api", "invalid piuio light index: {}", index);
            }
            break;
        }
        case rawinput::SMX_STAGE: {
            if (index < rawinput::SmxStageDevice::TOTAL_LIGHT_COUNT) {
                device->smxstageInfo->SetLightByIndex(index, static_cast<uint8_t>(value*255.f));
                device->output_pending = true;
            } else {
                log_warning("api", "invalid smx stage light index: {}", index);
            }
            break;
        }
        case rawinput::SMX_DEDICAB: {
            if (index < rawinput::SmxDedicabDevice::LIGHTS_COUNT) {
                device->smxdedicabInfo->SetLightByIndex(index, static_cast<uint8_t>(value * 255.f));
                device->output_pending = true;
            } else {
                log_warning("api", "invalid SMX dedicab light index: {}", index);
            }
            break;
        }
        default:
            break;
    }

    // unlock device
    device->mutex->unlock();
}

void GameAPI::Lights::writeLight(rawinput::RawInputManager *manager, Light &light, float value) {

    // clamp to range [0,1]
    value = CLAMP(value, 0.f, 1.f);

    // write to last state
    light.last_state = value;

    // get device
    auto &devid = light.getDeviceIdentifier();
    auto device = manager->devices_get(devid, false);

    // check device
    if (device) {

        // write state
        if (light.override_enabled) {
            writeLight(device, light.getIndex(), light.override_state);
        } else {
            writeLight(device, light.getIndex(), value);
        }
    }

    // alternatives
    for (auto &alternative : light.getAlternatives()) {
        if (light.override_enabled) {
            alternative.override_enabled = true;
            alternative.override_state = light.override_state;
            writeLight(manager, alternative, light.override_state);
        } else {
            alternative.override_enabled = false;
            writeLight(manager, alternative, value);
        }
    }
}

void Lights::writeLight(std::unique_ptr<rawinput::RawInputManager> &manager, Light &light, float value) {
    if (manager) {
        writeLight(manager.get(), light, value);
    }
}

float GameAPI::Lights::readLight(rawinput::Device *device, int index) {
    float ret = 0.f;

    // lock device
    device->mutex->lock();

    // check type
    switch (device->type) {
        case rawinput::HID: {
            auto hid = device->hidInfo;

            // find in buttons
            bool button_found = false;
            for (auto &button_states : hid->button_output_states) {
                if ((size_t) index < button_states.size()) {
                    ret = button_states[index] ? 1.f : 0.f;
                    button_found = true;
                    break;
                } else
                    index -= button_states.size();
            }

            // find in values
            if (!button_found) {
                auto value_states = &hid->value_output_states;
                if ((size_t) index < value_states->size()) {
                    ret = (*value_states)[index];
                }
            }

            break;
        }
        default:
            break;
    }

    // unlock device
    device->mutex->unlock();

    // return result
    return ret;
}

float GameAPI::Lights::readLight(rawinput::RawInputManager *manager, Light &light) {

    // check override
    if (light.override_enabled) {
        return light.override_state;
    }

    // just return last state since that reflects the last value being written
    return light.last_state;
}

float Lights::readLight(std::unique_ptr<rawinput::RawInputManager> &manager, Light &light) {
    if (manager) {
        return readLight(manager.get(), light);
    } else {

        // check override
        if (light.override_enabled) {
            return light.override_state;
        }

        // just return last state since that reflects the last value being written
        return light.last_state;
    }
}

std::vector<Option> GameAPI::Options::getOptions(const std::string &gameName) {
    return Config::getInstance().getOptions(gameName);
}

void GameAPI::Options::sortOptions(std::vector<Option> &options, const std::vector<OptionDefinition> &definitions) {
    std::vector<Option> sorted;
    bool option_found;

    for (const auto &definition : definitions) {
        option_found = false;

        for (auto &option : options) {
            if (definition.name == option.get_definition().name) {
                option_found = true;

                auto &new_option = sorted.emplace_back(option);
                new_option.set_definition(definition);

                break;
            }
        }

        if (!option_found) {
            sorted.emplace_back(definition);
        }
    }

    options = std::move(sorted);
}

static Buttons::State getMidiV2ButtonState(float on, float off) {
    if (on == 0.0) {
        return Buttons::State::BUTTON_NOT_PRESSED;
    } else if (off < on) {
        // if OFF was not observed strictly after ON, we can confidently say that the note
        // remains ON; in case of a tie (rarely in v2, all the time in v2_drum), prefer to keep note
        // off since that's better than a note stuck on
        return Buttons::State::BUTTON_PRESSED;
    } else {
        // otherwise, this is an ON-OFF sequence
        // check for time the most recent ON message
        //
        // if recent, consider the button to be on - even if there were OFF messages following it
        // this is needed to detect things like MIDI drums which send a quick ON-OFF sequence
        // between the game's polling period
        const auto now = get_performance_milliseconds();
        if ((now - on) < (double)rawinput::MIDI_NOTE_SUSTAIN) {
            return Buttons::State::BUTTON_PRESSED;
        } else {
            return Buttons::State::BUTTON_NOT_PRESSED;
        }
    }
}