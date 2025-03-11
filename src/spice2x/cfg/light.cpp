#include "light.h"

#include <utility>

#include "rawinput/piuio.h"
#include "rawinput/rawinput.h"
#include "rawinput/sextet.h"
#include "rawinput/smxdedicab.h"
#include "rawinput/smxstage.h"
#include "util/logging.h"

std::string Light::getDisplayString(rawinput::RawInputManager* manager) {

    // get index string
    std::string index_string = fmt::format("{:#x}", index);

    // device must be existing
    if (this->deviceIdentifier.empty()) {
        return "";
    }

    // get device
    auto device = manager->devices_get(this->deviceIdentifier);
    if (!device) {
        return "Device missing (" + index_string + ")";
    }

    // return string based on device type
    switch (device->type) {
        case rawinput::HID: {
            auto hid = device->hidInfo;
            unsigned int hid_index = index;

            // check button output caps
            if (hid_index < hid->button_output_caps_names.size()) {
                return hid->button_output_caps_names[hid_index] +
                       " (" + index_string + " - " + device->desc + ")";
            } else {
                hid_index -= hid->button_output_caps_names.size();
            }

            // check value output caps
            if (hid_index < hid->value_output_caps_names.size()) {
                return hid->value_output_caps_names[hid_index] +
                       " (" + index_string + " - " + device->desc + ")";
            }

            // not found
            return "Invalid Light (" + index_string + ")";
        }
        case rawinput::SEXTET_OUTPUT: {

            // get light name of sextet device
            if (index < rawinput::SextetDevice::LIGHT_COUNT) {
                return rawinput::SextetDevice::LIGHT_NAMES[index] + " (" + index_string + ")";
            }

            // not found
            return "Invalid Sextet Light (" + index_string + ")";
        }
        case rawinput::PIUIO_DEVICE: {

            // get light name of PIUIO device
            if (index < rawinput::PIUIO::PIUIO_MAX_NUM_OF_LIGHTS) {
                return rawinput::PIUIO::LIGHT_NAMES[index] + " (" + index_string + ")";
            }

            return "Invalid PIUIO Light (" + index_string + ")";
        }
        case rawinput::SMX_STAGE: {

            // get light name of SMX Stage device
            if (index < rawinput::SmxStageDevice::TOTAL_LIGHT_COUNT) {
                return rawinput::SmxStageDevice::GetLightNameByIndex(index) + " (" + index_string + ")";
            }

            return "Invalid SMX Stage Light (" + index_string + ")";
        }
        case rawinput::SMX_DEDICAB: {

            // get light name of SMX Dedicab device
            if (index < rawinput::SmxDedicabDevice::LIGHTS_COUNT) {
                return rawinput::SmxDedicabDevice::GetLightNameByIndex(index) + " (" + index_string + ")";
            }

            return "Invalid SMX Dedicab Light (" + index_string + ")";
        }
        case rawinput::DESTROYED:
            return "Unplugged device (" + index_string + ")";
        default:
            return "Unknown Light (" + index_string + ")";
    }
}
