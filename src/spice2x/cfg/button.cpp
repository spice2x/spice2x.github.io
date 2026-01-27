#include "button.h"

#include "rawinput/rawinput.h"
#include "util/logging.h"
#include "util/utils.h"

const char *ButtonAnalogTypeStr[] = {
        "None",
        "Positive",
        "Negative",
        "Hat Up",
        "Hat Upright",
        "Hat Right",
        "Hat Downright",
        "Hat Down",
        "Hat Downleft",
        "Hat Left",
        "Hat Upleft",
        "Hat Neutral",
        "MIDI Control Precision",
        "MIDI Control Single",
        "MIDI Control On/Off",
        "MIDI Pitch Down",
        "MIDI Pitch Up",
        "Any Direction",
};

std::string Button::getVKeyString() {
    switch (this->getVKey() % 256) {
        case 0x01:
            return "Left MB";
        case 0x02:
            return "Right MB";
        case 0x04:
            return "Middle MB";
        case 0x05:
            return "X1 MB";
        case 0x06:
            return "X2 MB";
        case 0x08:
            return "Backspace";
        case 0x09:
            return "Tab";
        case 0x0C:
            return "Clear";
        case 0x0D:
            return "Enter";
        case 0x10:
            return "Shift";
        case 0x11:
            return "Ctrl";
        case 0x12:
            if (this->getVKey() > 255)
                return "AltGr";
            else
                return "Alt";
        case 0x13:
            return "Pause";
        case 0x14:
            return "Caps Lock";
        case 0x1B:
            return "Escape";
        case 0x20:
            return "Space";
        case 0x21:
            return "Page Up";
        case 0x22:
            return "Page Down";
        case 0x23:
            return "End";
        case 0x24:
            return "Home";
        case 0x25:
            return "Left";
        case 0x26:
            return "Up";
        case 0x27:
            return "Right";
        case 0x28:
            return "Down";
        case 0x2C:
            return "Prt Scr";
        case 0x2D:
            return "Insert";
        case 0x2E:
            return "Delete";
        case 0x30:
            return "0";
        case 0x31:
            return "1";
        case 0x32:
            return "2";
        case 0x33:
            return "3";
        case 0x34:
            return "4";
        case 0x35:
            return "5";
        case 0x36:
            return "6";
        case 0x37:
            return "7";
        case 0x38:
            return "8";
        case 0x39:
            return "9";
        case 0x41:
            return "A";
        case 0x42:
            return "B";
        case 0x43:
            return "C";
        case 0x44:
            return "D";
        case 0x45:
            return "E";
        case 0x46:
            return "F";
        case 0x47:
            return "G";
        case 0x48:
            return "H";
        case 0x49:
            return "I";
        case 0x4A:
            return "J";
        case 0x4B:
            return "K";
        case 0x4C:
            return "L";
        case 0x4D:
            return "M";
        case 0x4E:
            return "N";
        case 0x4F:
            return "O";
        case 0x50:
            return "P";
        case 0x51:
            return "Q";
        case 0x52:
            return "R";
        case 0x53:
            return "S";
        case 0x54:
            return "T";
        case 0x55:
            return "U";
        case 0x56:
            return "V";
        case 0x57:
            return "W";
        case 0x58:
            return "X";
        case 0x59:
            return "Y";
        case 0x5A:
            return "Z";
        case 0x5B:
            return "Left Windows";
        case 0x5C:
            return "Right Windows";
        case 0x5D:
            return "Apps";
        case 0x60:
            return "Num 0";
        case 0x61:
            return "Num 1";
        case 0x62:
            return "Num 2";
        case 0x63:
            return "Num 3";
        case 0x64:
            return "Num 4";
        case 0x65:
            return "Num 5";
        case 0x66:
            return "Num 6";
        case 0x67:
            return "Num 7";
        case 0x68:
            return "Num 8";
        case 0x69:
            return "Num 9";
        case 0x6A:
            return "*";
        case 0x6B:
            return "+";
        case 0x6C:
            return "Seperator";
        case 0x6D:
            return "-";
        case 0x6E:
            return ".";
        case 0x6F:
            return "/";
        case 0x70:
            return "F1";
        case 0x71:
            return "F2";
        case 0x72:
            return "F3";
        case 0x73:
            return "F4";
        case 0x74:
            return "F5";
        case 0x75:
            return "F6";
        case 0x76:
            return "F7";
        case 0x77:
            return "F8";
        case 0x78:
            return "F9";
        case 0x79:
            return "F10";
        case 0x7A:
            return "F11";
        case 0x7B:
            return "F12";
        case 0x7C:
            return "F13";
        case 0x7D:
            return "F14";
        case 0x7E:
            return "F15";
        case 0x7F:
            return "F16";
        case 0x80:
            return "F17";
        case 0x81:
            return "F18";
        case 0x82:
            return "F19";
        case 0x83:
            return "F20";
        case 0x84:
            return "F21";
        case 0x85:
            return "F22";
        case 0x86:
            return "F23";
        case 0x87:
            return "F24";
        case 0x90:
            return "Num Lock";
        case 0x91:
            return "Scroll Lock";
        case 0xA0:
            return "Left Shift";
        case 0xA1:
            return "Right Shift";
        case 0xA2:
            return "Left Control";
        case 0xA3:
            return "Right Control";
        case 0xA4:
            return "Left Menu";
        case 0xA5:
            return "Right Menu";
        default:

            // check win API
            char keyName[128];
            if (GetKeyNameText((LONG) (MapVirtualKey(vKey, MAPVK_VK_TO_VSC) << 16), keyName, 128))
                return std::string(keyName);
            return "Unknown";
    }
}

std::string Button::getMidiNoteString() {
    static const std::string note_names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

    int channel;
    int index;
    this->getMidiVKey(channel, index);
    return fmt::format("{}{}", note_names[index % 12], ((index / 12) - 1));
}

std::string Button::getDisplayString(rawinput::RawInputManager* manager) {

    // get VKey string
    auto vKey = (uint16_t) this->getVKey();
    std::string vKeyString = fmt::format("{:#x}", vKey);

    // device must be existing
    if (this->device_identifier.empty() && vKey == INVALID_VKEY) {
        return "";
    }

    if (this->isNaive()) {
        return this->getVKeyString() + " (Naive, " + vKeyString + ")";
    } else {
        auto device = manager->devices_get(this->device_identifier);
        if (!device) {
            return "Device missing (" + vKeyString + ")";
        }

        std::lock_guard<std::mutex> lock(*device->mutex);

        switch (device->type) {
            case rawinput::MOUSE: {
                const char *btn = "Unknown";
                static const std::array<const char *, 8>MOUSE_NAMES = {
                    "Left Mouse",
                    "Right Mouse",
                    "Middle Mouse",
                    "Mouse 1",
                    "Mouse 2",
                    "Mouse 3",
                    "Mouse 4",
                    "Mouse 5",
                    };
                if (vKey < MOUSE_NAMES.size()) {
                    btn = MOUSE_NAMES[vKey];
                }
                return fmt::format("{} ({})", btn, device->desc);
            }
            case rawinput::KEYBOARD:
                return this->getVKeyString() + " (" + device->desc + ")";
            case rawinput::HID: {
                auto hid = device->hidInfo;
                switch (this->analog_type) {
                    case BAT_NONE:
                        if (vKey < hid->button_caps_names.size())
                            return hid->button_caps_names[vKey] + " (" + device->desc + ")";
                        else
                            return "Invalid button (" + device->desc + ")";
                    case BAT_NEGATIVE:
                    case BAT_POSITIVE:
                    case BAT_ANY: {
                        const char *sign;
                        if (this->analog_type == BAT_NEGATIVE) {
                            sign = "-";
                        } else if (this->analog_type == BAT_POSITIVE) {
                            sign = "+";
                        } else {
                            sign = "*";
                        }
                        if (vKey < hid->value_caps_names.size()) {
                            return hid->value_caps_names[vKey] + sign + " (" + device->desc + ")";
                        } else {
                            return "Invalid analog (" + device->desc + ")";
                        }
                    }
                    case BAT_HS_UP:
                        return "Hat Up (" + device->desc + ")";
                    case BAT_HS_UPRIGHT:
                        return "Hat UpRight (" + device->desc + ")";
                    case BAT_HS_RIGHT:
                        return "Hat Right (" + device->desc + ")";
                    case BAT_HS_DOWNRIGHT:
                        return "Hat DownRight (" + device->desc + ")";
                    case BAT_HS_DOWN:
                        return "Hat Down (" + device->desc + ")";
                    case BAT_HS_DOWNLEFT:
                        return "Hat DownLeft (" + device->desc + ")";
                    case BAT_HS_LEFT:
                        return "Hat Left (" + device->desc + ")";
                    case BAT_HS_UPLEFT:
                        return "Hat UpLeft (" + device->desc + ")";
                    case BAT_HS_NEUTRAL:
                        return "Hat Neutral (" + device->desc + ")";
                    default:
                        return "Unknown analog type (" + device->desc + ")";
                }
            }
            case rawinput::MIDI: {
                int channel = 0;
                int ctrl = 0;
                this->getMidiVKey(channel, ctrl);
                switch (this->analog_type) {
                    // update strings in analog.cpp as well
                    case BAT_NONE: {
                        const auto note = this->getMidiNoteString();
                        return fmt::format("MIDI Note Ch.{} #{} {} ({})", channel, ctrl, note, device->desc);
                    }
                    case BAT_MIDI_CTRL_PRECISION: {
                        return fmt::format("MIDI Prec Ctrl Ch.{} CC#{} ({})", channel, ctrl, device->desc);
                    }
                    case BAT_MIDI_CTRL_SINGLE: {
                        return fmt::format("MIDI Ctrl Ch.{} CC#{} ({})", channel, ctrl, device->desc);
                    }
                    case BAT_MIDI_CTRL_ONOFF: {
                        return fmt::format("MIDI OnOff Ch.{} CC#{} ({})", channel, ctrl, device->desc);
                    }
                    case BAT_MIDI_PITCH_DOWN:
                        return fmt::format("MIDI Pitch Down Ch.{} ({})", channel, device->desc);
                    case BAT_MIDI_PITCH_UP:
                        return fmt::format("MIDI Pitch Up Ch.{} ({})", channel, device->desc);
                    default:
                        return "MIDI Unknown " + vKeyString + " (" + device->desc + ")";
                }
                return "";
            }
            case rawinput::PIUIO_DEVICE:
                return "PIUIO " + vKeyString;
            case rawinput::DESTROYED:
                return "Device unplugged (" + vKeyString + ")";
            default:
                return "Unknown device type (" + vKeyString + ")";
        }
    }
}

void Button::getMidiVKey(int& channel, int& index) {
    switch (this->analog_type) {
        // update strings in analog.cpp as well
        case BAT_NONE:
            channel = (vKey / 0x80) + 1;
            index = vKey & 0x7f;
            break;
        case BAT_MIDI_CTRL_PRECISION:
            channel = (vKey / 32) + 1;
            index = (vKey % 32);
            break;
        case BAT_MIDI_CTRL_SINGLE:
            channel = (vKey / 44) + 1;
            index = (vKey % 44);
            if (index <= 25) {
                index += 0x46; // single byte range
            } else {
                index = index - 26 + 0x66; // undefined single byte range
            }
            break;
        case BAT_MIDI_CTRL_ONOFF:
            channel = (vKey / 6) + 1;
            index = (vKey % 6) + 0x40;
            break;
        case BAT_MIDI_PITCH_DOWN:
        case BAT_MIDI_PITCH_UP:
            channel = vKey + 1;
            index = 0;
            break;
        default:
            channel = 0;
            index = 0;
            break;
    }
}

void Button::setMidiVKey(rawinput::RawInputManager* manager, bool is_note, int channel, int index) {
    int vKey = 0;
    if (is_note) {
        vKey = (channel - 1) * 0x80 + index;
        this->setVKey(vKey);
        this->setAnalogType(BAT_NONE);

        // ensure that velocity threshold is read back from what rawinput has for other bindings
        if (manager && !this->device_identifier.empty()) {
            auto device = manager->devices_get(this->device_identifier);
            if (device &&
                device->midiInfo &&
                (size_t)vKey < device->midiInfo->v2_velocity_threshold.size()) {
                this->setVelocityThreshold(device->midiInfo->v2_velocity_threshold[vKey]);
            }
        }
        return;
    }

    if (channel < 1 || 16 < channel) {
        this->setVKey(0);
        this->setAnalogType(BAT_NONE);
        return;
    }
    if (index < 0 || 127 < index) {
        this->setVKey(0);
        this->setAnalogType(BAT_NONE);
        return;
    }

    // continuous controller MSB
    if (0x00 <= index && index <= 0x1F) {
        vKey = (channel - 1) * 32 + index;
        this->setVKey(vKey);
        this->setAnalogType(BAT_MIDI_CTRL_PRECISION);
        return;
    }

    // continuous controller LSB
    if (0x20 <= index && index <= 0x3F) {
        vKey = (channel - 1) * 32 + index - 0x20;
        this->setVKey(vKey);
        this->setAnalogType(BAT_MIDI_CTRL_PRECISION);
        return;
    }

    // on/off controls
    if (0x40 <= index && index <= 0x45) {
        vKey = (channel - 1) * 6 + (index - 0x40);
        this->setVKey(vKey);
        this->setAnalogType(BAT_MIDI_CTRL_ONOFF);
        return;
    }

    // single byte controllers
    if (0x46 <= index && index <= 0x5F) {
        vKey = (channel - 1) * 44;
        vKey += index - 0x46; // single byte range
        this->setVKey(vKey);
        this->setAnalogType(BAT_MIDI_CTRL_SINGLE);
        return;
    }

    // undefined single byte controllers
    if (0x66 <= index && index <= 0x77) {
        vKey = (channel - 1) * 44;
        vKey += index - 0x66 + (0x5F - 0x46 + 1) ; // undefined single byte range
        this->setVKey(vKey);
        this->setAnalogType(BAT_MIDI_CTRL_SINGLE);
        return;
    }
}

#define HAT_SWITCH_INCREMENT (1.f / 7)

void Button::getHatSwitchValues(float analog_state, ButtonAnalogType* buffer) {
    // rawinput converts neutral hat switch values to a negative value
    if (analog_state < 0.f) {
        buffer[0] = BAT_HS_NEUTRAL;
        buffer[1] = BAT_NONE;
        buffer[2] = BAT_NONE;
        return;
    }
    if (analog_state < 0 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_UP;
        buffer[1] = BAT_NONE;
        buffer[2] = BAT_NONE;
        return;
    }
    if (analog_state < 1 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_UPRIGHT;
        buffer[1] = BAT_HS_UP;
        buffer[2] = BAT_HS_RIGHT;
        return;
    }
    if (analog_state < 2 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_RIGHT;
        buffer[1] = BAT_NONE;
        buffer[2] = BAT_NONE;
        return;
    }
    if (analog_state < 3 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_DOWNRIGHT;
        buffer[1] = BAT_HS_RIGHT;
        buffer[2] = BAT_HS_DOWN;
        return;
    }
    if (analog_state < 4 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_DOWN;
        buffer[1] = BAT_NONE;
        buffer[2] = BAT_NONE;
        return;
    }
    if (analog_state < 5 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_DOWNLEFT;
        buffer[1] = BAT_HS_DOWN;
        buffer[2] = BAT_HS_LEFT;
        return;
    }
    if (analog_state < 6 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_LEFT;
        buffer[1] = BAT_NONE;
        buffer[2] = BAT_NONE;
        return;
    }
    if (analog_state < 7 * HAT_SWITCH_INCREMENT + 0.001f) {
        buffer[0] = BAT_HS_UPLEFT;
        buffer[1] = BAT_HS_LEFT;
        buffer[2] = BAT_HS_UP;
        return;
    }
    
    buffer[0] = BAT_HS_NEUTRAL;
    buffer[1] = BAT_NONE;
    buffer[2] = BAT_NONE;
    return;
}
